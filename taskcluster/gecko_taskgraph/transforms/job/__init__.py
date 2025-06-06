# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Convert a job description into a task description.

Jobs descriptions are similar to task descriptions, but they specify how to run
the job at a higher level, using a "run" field that can be interpreted by
run-using handlers in `taskcluster/gecko_taskgraph/transforms/job`.
"""

import logging
import os

import mozpack.path as mozpath
from packaging.version import Version
from taskgraph.transforms.base import TransformSequence
from taskgraph.transforms.run import rewrite_when_to_optimization
from taskgraph.util import json
from taskgraph.util.copy import deepcopy
from taskgraph.util.python_path import import_sibling_modules
from taskgraph.util.schema import Schema, validate_schema
from taskgraph.util.taskcluster import get_artifact_prefix
from voluptuous import Any, Coerce, Exclusive, Extra, Optional, Required

from gecko_taskgraph.transforms.cached_tasks import order_tasks
from gecko_taskgraph.transforms.task import task_description_schema
from gecko_taskgraph.util.workertypes import worker_type_implementation

logger = logging.getLogger(__name__)

# Schema for a build description
job_description_schema = Schema(
    {
        # The name of the job and the job's label.  At least one must be specified,
        # and the label will be generated from the name if necessary, by prepending
        # the kind.
        Optional("name"): str,
        Optional("label"): str,
        # the following fields are passed directly through to the task description,
        # possibly modified by the run implementation.  See
        # taskcluster/gecko_taskgraph/transforms/task.py for the schema details.
        Required("description"): task_description_schema["description"],
        Optional("attributes"): task_description_schema["attributes"],
        Optional("task-from"): task_description_schema["task-from"],
        Optional("dependencies"): task_description_schema["dependencies"],
        Optional("if-dependencies"): task_description_schema["if-dependencies"],
        Optional("soft-dependencies"): task_description_schema["soft-dependencies"],
        Optional("if-dependencies"): task_description_schema["if-dependencies"],
        Optional("requires"): task_description_schema["requires"],
        Optional("expires-after"): task_description_schema["expires-after"],
        Optional("expiration-policy"): task_description_schema["expiration-policy"],
        Optional("routes"): task_description_schema["routes"],
        Optional("scopes"): task_description_schema["scopes"],
        Optional("tags"): task_description_schema["tags"],
        Optional("extra"): task_description_schema["extra"],
        Optional("treeherder"): task_description_schema["treeherder"],
        Optional("index"): task_description_schema["index"],
        Optional("run-on-projects"): task_description_schema["run-on-projects"],
        Optional("shipping-phase"): task_description_schema["shipping-phase"],
        Optional("shipping-product"): task_description_schema["shipping-product"],
        Optional("always-target"): task_description_schema["always-target"],
        Exclusive("optimization", "optimization"): task_description_schema[
            "optimization"
        ],
        Optional("use-sccache"): task_description_schema["use-sccache"],
        Optional("use-python"): Any("system", "default", Coerce(Version)),
        # Fetch uv binary and add it to PATH
        Optional("use-uv"): bool,
        Optional("priority"): task_description_schema["priority"],
        # The "when" section contains descriptions of the circumstances under which
        # this task should be included in the task graph.  This will be converted
        # into an optimization, so it cannot be specified in a job description that
        # also gives 'optimization'.
        Exclusive("when", "optimization"): Any(
            None,
            {
                # This task only needs to be run if a file matching one of the given
                # patterns has changed in the push.  The patterns use the mozpack
                # match function (python/mozbuild/mozpack/path.py).
                Optional("files-changed"): [str],
            },
        ),
        # A list of artifacts to install from 'fetch' tasks.
        Optional("fetches"): {
            str: [
                str,
                {
                    Required("artifact"): str,
                    Optional("dest"): str,
                    Optional("extract"): bool,
                    Optional("verify-hash"): bool,
                },
            ],
        },
        # A description of how to run this job.
        "run": {
            # The key to a job implementation in a peer module to this one
            "using": str,
            # Base work directory used to set up the task.
            Optional("workdir"): str,
            # Any remaining content is verified against that job implementation's
            # own schema.
            Extra: object,
        },
        Required("worker-type"): task_description_schema["worker-type"],
        # This object will be passed through to the task description, with additions
        # provided by the job's run-using function
        Optional("worker"): dict,
    }
)

transforms = TransformSequence()
transforms.add_validate(job_description_schema)
transforms.add(rewrite_when_to_optimization)


@transforms.add
def set_implementation(config, jobs):
    for job in jobs:
        impl, os = worker_type_implementation(
            config.graph_config, config.params, job["worker-type"]
        )
        if os:
            job.setdefault("tags", {})["os"] = os
        if impl:
            job.setdefault("tags", {})["worker-implementation"] = impl
        worker = job.setdefault("worker", {})
        assert "implementation" not in worker
        worker["implementation"] = impl
        if os:
            worker["os"] = os
        yield job


@transforms.add
def set_label(config, jobs):
    for job in jobs:
        if "label" not in job:
            if "name" not in job:
                raise Exception("job has neither a name nor a label")
            job["label"] = "{}-{}".format(config.kind, job["name"])
        if job.get("name"):
            del job["name"]
        yield job


@transforms.add
def make_task_description(config, jobs):
    """Given a build description, create a task description"""
    # import plugin modules first, before iterating over jobs
    import_sibling_modules(exceptions=("common.py",))

    for job in jobs:
        # only docker-worker uses a fixed absolute path to find directories
        if job["worker"]["implementation"] == "docker-worker":
            job["run"].setdefault("workdir", "/builds/worker")

        taskdesc = deepcopy(job)

        # fill in some empty defaults to make run implementations easier
        taskdesc.setdefault("attributes", {})
        taskdesc.setdefault("dependencies", {})
        taskdesc.setdefault("if-dependencies", [])
        taskdesc.setdefault("soft-dependencies", [])
        taskdesc.setdefault("routes", [])
        taskdesc.setdefault("scopes", [])
        taskdesc.setdefault("extra", {})

        # give the function for job.run.using on this worker implementation a
        # chance to set up the task description.
        configure_taskdesc_for_run(
            config, job, taskdesc, job["worker"]["implementation"]
        )
        del taskdesc["run"]

        # yield only the task description, discarding the job description
        yield taskdesc


def get_attribute(dict, key, attributes, attribute_name):
    """Get `attribute_name` from the given `attributes` dict, and if there
    is a corresponding value, set `key` in `dict` to that value."""
    value = attributes.get(attribute_name)
    if value is not None:
        dict[key] = value


def get_platform(job):
    if "win" in job["worker"]["os"]:
        return "win64"
    elif "linux" in job["worker"]["os"]:
        platform = "linux64"
        if "aarch64" in job["worker-type"] or "arm64" in job["worker-type"]:
            return f"{platform}-aarch64"
        return platform
    elif "macosx" in job["worker"]["os"]:
        return "macosx64"
    else:
        raise ValueError(f"unexpected worker.os value {job['worker']['os']}")


@transforms.add
def use_system_python(config, jobs):
    for job in jobs:
        taskcluster_python = job.pop("use-python", "system")
        if taskcluster_python == "system":
            yield job
        else:
            if taskcluster_python == "default":
                python_version = "python"  # the taskcluster default alias
            else:
                python_version = f"python-{taskcluster_python}"

            fetches = job.setdefault("fetches", {})
            toolchain = fetches.setdefault("toolchain", [])
            platform = get_platform(job)

            toolchain.append(f"{platform}-{python_version}")

            worker = job.setdefault("worker", {})
            env = worker.setdefault("env", {})

            moz_fetches_dir = env.get("MOZ_FETCHES_DIR", "fetches")
            moz_python_home = mozpath.join(moz_fetches_dir, "python")
            env["MOZ_PYTHON_HOME"] = moz_python_home

            yield job


@transforms.add
def use_uv(config, jobs):
    for job in jobs:
        if not job.pop("use-uv", False):
            yield job
        else:
            fetches = job.setdefault("fetches", {})
            toolchain = fetches.setdefault("toolchain", [])
            platform = get_platform(job)

            toolchain.append(f"{platform}-uv")

            worker = job.setdefault("worker", {})
            env = worker.setdefault("env", {})
            moz_fetches_dir = env.get("MOZ_FETCHES_DIR", "fetches")
            env["MOZ_UV_HOME"] = os.path.join(moz_fetches_dir, "uv")

            yield job


@transforms.add
def use_fetches(config, jobs):
    artifact_names = {}
    extra_env = {}
    should_extract = {}
    aliases = {}
    tasks = []

    if config.kind in ("toolchain", "fetch"):
        jobs = list(jobs)
        tasks.extend((config.kind, j) for j in jobs)

    tasks.extend(
        (task.kind, task.__dict__)
        for task in config.kind_dependencies_tasks.values()
        if task.kind in ("fetch", "toolchain")
    )
    for kind, task in tasks:
        get_attribute(
            artifact_names, task["label"], task["attributes"], f"{kind}-artifact"
        )
        get_attribute(extra_env, task["label"], task["attributes"], f"{kind}-env")
        get_attribute(
            should_extract, task["label"], task["attributes"], f"{kind}-extract"
        )
        value = task["attributes"].get(f"{kind}-alias")
        if not value:
            value = []
        elif isinstance(value, str):
            value = [value]
        for alias in value:
            fully_qualified = f"{kind}-{alias}"
            label = task["label"]
            if fully_qualified == label:
                raise Exception(f"The alias {alias} of task {label} points to itself!")
            aliases[fully_qualified] = label

    artifact_prefixes = {}
    for job in order_tasks(config, jobs):
        artifact_prefixes[job["label"]] = get_artifact_prefix(job)

        fetches = job.pop("fetches", None)
        if not fetches:
            yield job
            continue

        job_fetches = []
        name = job.get("name") or job.get("label").replace(f"{config.kind}-", "")
        dependencies = job.setdefault("dependencies", {})
        worker = job.setdefault("worker", {})
        env = worker.setdefault("env", {})
        prefix = get_artifact_prefix(job)
        has_sccache = False
        for kind, artifacts in fetches.items():
            if kind in ("fetch", "toolchain"):
                for fetch_name in artifacts:
                    label = f"{kind}-{fetch_name}"
                    label = aliases.get(label, label)
                    if label not in artifact_names:
                        raise Exception(
                            f"Missing fetch job for {config.kind}-{name}: {fetch_name}"
                        )
                    if label in extra_env:
                        env.update(extra_env[label])

                    path = artifact_names[label]

                    dependencies[label] = label
                    job_fetches.append(
                        {
                            "artifact": path,
                            "task": f"<{label}>",
                            "extract": should_extract.get(label, True),
                        }
                    )

                    if kind == "toolchain" and fetch_name.endswith("-sccache"):
                        has_sccache = True
            else:
                if kind not in dependencies:
                    raise Exception(
                        f"{name} can't fetch {kind} artifacts because "
                        f"it has no {kind} dependencies!"
                    )
                dep_label = dependencies[kind]
                if dep_label in artifact_prefixes:
                    prefix = artifact_prefixes[dep_label]
                else:
                    if dep_label not in config.kind_dependencies_tasks:
                        raise Exception(
                            f"{name} can't fetch {kind} artifacts because "
                            f"there are no tasks with label {dependencies[kind]} in kind dependencies!"
                        )

                    prefix = get_artifact_prefix(
                        config.kind_dependencies_tasks[dep_label]
                    )

                for artifact in artifacts:
                    if isinstance(artifact, str):
                        path = artifact
                        dest = None
                        extract = True
                        verify_hash = False
                    else:
                        path = artifact["artifact"]
                        dest = artifact.get("dest")
                        extract = artifact.get("extract", True)
                        verify_hash = artifact.get("verify-hash", False)

                    fetch = {
                        "artifact": (
                            f"{prefix}/{path}" if not path.startswith("/") else path[1:]
                        ),
                        "task": f"<{kind}>",
                        "extract": extract,
                    }
                    if dest is not None:
                        fetch["dest"] = dest
                    if verify_hash:
                        fetch["verify-hash"] = verify_hash
                    job_fetches.append(fetch)

        if job.get("use-sccache") and not has_sccache:
            raise Exception("Must provide an sccache toolchain if using sccache.")

        job_artifact_prefixes = {
            mozpath.dirname(fetch["artifact"])
            for fetch in job_fetches
            if not fetch["artifact"].startswith("public/")
        }
        if job_artifact_prefixes:
            # Use taskcluster-proxy and request appropriate scope.  For example, add
            # 'scopes: [queue:get-artifact:path/to/*]' for 'path/to/artifact.tar.xz'.
            worker["taskcluster-proxy"] = True
            for prefix in sorted(job_artifact_prefixes):
                scope = f"queue:get-artifact:{prefix}/*"
                if scope not in job.setdefault("scopes", []):
                    job["scopes"].append(scope)

        artifacts = {}
        for f in job_fetches:
            _, __, artifact = f["artifact"].rpartition("/")
            if "dest" in f:
                artifact = f"{f['dest']}/{artifact}"
            task = f["task"][1:-1]
            if artifact in artifacts:
                raise Exception(
                    f"Task {name} depends on {artifacts[artifact]} and {task} "
                    f"that both provide {artifact}"
                )
            artifacts[artifact] = task

        env["MOZ_FETCHES"] = {
            "task-reference": json.dumps(
                sorted(job_fetches, key=lambda x: sorted(x.items())), sort_keys=True
            )
        }

        # The path is normalized to an absolute path in run-task
        env.setdefault("MOZ_FETCHES_DIR", "fetches")

        yield job


# A registry of all functions decorated with run_job_using
registry = {}


def run_job_using(worker_implementation, run_using, schema=None, defaults={}):
    """Register the decorated function as able to set up a task description for
    jobs with the given worker implementation and `run.using` property.  If
    `schema` is given, the job's run field will be verified to match it.

    The decorated function should have the signature `using_foo(config, job, taskdesc)`
    and should modify the task description in-place.  The skeleton of
    the task description is already set up, but without a payload."""

    def wrap(func):
        for_run_using = registry.setdefault(run_using, {})
        if worker_implementation in for_run_using:
            raise Exception(
                f"run_job_using({run_using!r}, {worker_implementation!r}) already exists: {for_run_using[worker_implementation]!r}"
            )
        for_run_using[worker_implementation] = (func, schema, defaults)
        return func

    return wrap


@run_job_using(
    "always-optimized", "always-optimized", Schema({"using": "always-optimized"})
)
def always_optimized(config, job, taskdesc):
    pass


def configure_taskdesc_for_run(config, job, taskdesc, worker_implementation):
    """
    Run the appropriate function for this job against the given task
    description.

    This will raise an appropriate error if no function exists, or if the job's
    run is not valid according to the schema.
    """
    run_using = job["run"]["using"]
    if run_using not in registry:
        raise Exception(f"no functions for run.using {run_using!r}")

    if worker_implementation not in registry[run_using]:
        raise Exception(
            f"no functions for run.using {run_using!r} on {worker_implementation!r}"
        )

    func, schema, defaults = registry[run_using][worker_implementation]
    for k, v in defaults.items():
        job["run"].setdefault(k, v)

    if schema:
        validate_schema(
            schema,
            job["run"],
            "In job.run using {!r}/{!r} for job {!r}:".format(
                job["run"]["using"], worker_implementation, job["label"]
            ),
        )
    func(config, job, taskdesc)

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const NET_STRINGS_URI = "devtools/client/locales/netmonitor.properties";
const SVG_NS = "http://www.w3.org/2000/svg";
const PI = Math.PI;
const TAU = PI * 2;
const EPSILON = 0.0000001;
const NAMED_SLICE_MIN_ANGLE = TAU / 8;
const NAMED_SLICE_TEXT_DISTANCE_RATIO = 1.9;
const HOVERED_SLICE_TRANSLATE_DISTANCE_RATIO = 20;

const EventEmitter = require("resource://devtools/shared/event-emitter.js");
const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");
const L10N = new LocalizationHelper(NET_STRINGS_URI);

/**
 * A factory for creating charts.
 * Example usage: let myChart = Chart.Pie(document, { ... });
 */
var Chart = {
  Pie: createPieChart,
  Table: createTableChart,
  PieTable: createPieTableChart,
};

/**
 * A simple pie chart proxy for the underlying view.
 * Each item in the `slices` property represents a [data, node] pair containing
 * the data used to create the slice and the Node displaying it.
 *
 * @param Node node
 *        The node representing the view for this chart.
 */
function PieChart(node) {
  this.node = node;
  this.slices = new WeakMap();
  EventEmitter.decorate(this);
}

/**
 * A simple table chart proxy for the underlying view.
 * Each item in the `rows` property represents a [data, node] pair containing
 * the data used to create the row and the Node displaying it.
 *
 * @param Node node
 *        The node representing the view for this chart.
 */
function TableChart(node) {
  this.node = node;
  this.rows = new WeakMap();
  EventEmitter.decorate(this);
}

/**
 * A simple pie+table chart proxy for the underlying view.
 *
 * @param Node node
 *        The node representing the view for this chart.
 * @param PieChart pie
 *        The pie chart proxy.
 * @param TableChart table
 *        The table chart proxy.
 */
function PieTableChart(node, pie, table) {
  this.node = node;
  this.pie = pie;
  this.table = table;
  EventEmitter.decorate(this);
}

/**
 * Creates the DOM for a pie+table chart.
 *
 * @param Document document
 *        The document responsible with creating the DOM.
 * @param object
 *        An object containing all or some of the following properties:
 *          - title: a string displayed as the table chart's (description)/local
 *          - diameter: the diameter of the pie chart, in pixels
 *          - data: an array of items used to display each slice in the pie
 *                  and each row in the table;
 *                  @see `createPieChart` and `createTableChart` for details.
 *          - strings: @see `createTableChart` for details.
 *          - totals: @see `createTableChart` for details.
 *          - sorted: a flag specifying if the `data` should be sorted
 *                    ascending by `size`.
 * @return PieTableChart
 *         A pie+table chart proxy instance, which emits the following events:
 *           - "mouseover", when the mouse enters a slice or a row
 *           - "mouseout", when the mouse leaves a slice or a row
 *           - "click", when the mouse enters a slice or a row
 */
function createPieTableChart(
  document,
  { title, diameter, data, strings, totals, sorted, header }
) {
  if (data && sorted) {
    data = data.slice().sort((a, b) => +(a.size < b.size));
  }

  const pie = Chart.Pie(document, {
    width: diameter,
    data,
  });

  const table = Chart.Table(document, {
    title,
    data,
    strings,
    totals,
    header,
  });

  const container = document.createElement("div");
  container.className = "pie-table-chart-container";
  container.appendChild(pie.node);
  container.appendChild(table.node);

  const proxy = new PieTableChart(container, pie, table);

  pie.on("click", item => {
    proxy.emit("click", item);
  });

  table.on("click", item => {
    proxy.emit("click", item);
  });

  pie.on("mouseover", item => {
    proxy.emit("mouseover", item);
    if (table.rows.has(item)) {
      table.rows.get(item).setAttribute("focused", "");
    }
  });

  pie.on("mouseout", item => {
    proxy.emit("mouseout", item);
    if (table.rows.has(item)) {
      table.rows.get(item).removeAttribute("focused");
    }
  });

  table.on("mouseover", item => {
    proxy.emit("mouseover", item);
    if (pie.slices.has(item)) {
      pie.slices.get(item).setAttribute("focused", "");
    }
  });

  table.on("mouseout", item => {
    proxy.emit("mouseout", item);
    if (pie.slices.has(item)) {
      pie.slices.get(item).removeAttribute("focused");
    }
  });

  return proxy;
}

/**
 * Creates the DOM for a pie chart based on the specified properties.
 *
 * @param Document document
 *        The document responsible with creating the DOM.
 * @param object
 *        An object containing all or some of the following properties:
 *          - data: an array of items used to display each slice; all the items
 *                  should be objects containing a `size` and a `label` property.
 *                  e.g: [{
 *                    size: 1,
 *                    label: "foo"
 *                  }, {
 *                    size: 2,
 *                    label: "bar"
 *                  }];
 *          - width: the width of the chart, in pixels
 *          - height: optional, the height of the chart, in pixels.
 *          - centerX: optional, the X-axis center of the chart, in pixels.
 *          - centerY: optional, the Y-axis center of the chart, in pixels.
 *          - radius: optional, the radius of the chart, in pixels.
 * @return PieChart
 *         A pie chart proxy instance, which emits the following events:
 *           - "mouseover", when the mouse enters a slice
 *           - "mouseout", when the mouse leaves a slice
 *           - "click", when the mouse clicks a slice
 */
function createPieChart(
  document,
  { data, width, height, centerX, centerY, radius }
) {
  height = height || width;
  centerX = centerX || width / 2;
  centerY = centerY || height / 2;
  radius = radius || (width + height) / 4;
  let isPlaceholder = false;

  // If there's no data available, display an empty placeholder.
  if (!data) {
    data = loadingPieChartData();
    isPlaceholder = true;
  }
  if (!data.length) {
    data = emptyPieChartData();
    isPlaceholder = true;
  }

  const container = document.createElementNS(SVG_NS, "svg");
  container.setAttribute(
    "class",
    "generic-chart-container pie-chart-container"
  );

  container.setAttribute("width", width);
  container.setAttribute("height", height);
  container.setAttribute("viewBox", "0 0 " + width + " " + height);
  container.setAttribute("slices", data.length);
  container.setAttribute("placeholder", isPlaceholder);
  container.setAttribute("role", "group");
  container.setAttribute("aria-label", L10N.getStr("pieChart.ariaLabel"));

  const slicesGroup = document.createElementNS(SVG_NS, "g");
  slicesGroup.setAttribute("role", "list");
  container.append(slicesGroup);

  const proxy = new PieChart(container);

  const total = data.reduce((acc, e) => acc + e.size, 0);
  const angles = data.map(e => (e.size / total) * (TAU - EPSILON));
  const largest = data.reduce((a, b) => (a.size > b.size ? a : b));
  const smallest = data.reduce((a, b) => (a.size < b.size ? a : b));

  const textDistance = radius / NAMED_SLICE_TEXT_DISTANCE_RATIO;
  const translateDistance = radius / HOVERED_SLICE_TRANSLATE_DISTANCE_RATIO;
  let startAngle = TAU;
  let endAngle = 0;
  let midAngle = 0;
  radius -= translateDistance;

  for (let i = data.length - 1; i >= 0; i--) {
    const sliceInfo = data[i];
    const sliceAngle = angles[i];

    const sliceNode = document.createElementNS(SVG_NS, "g");
    sliceNode.setAttribute("role", "listitem");
    slicesGroup.append(sliceNode);

    const interactiveNodeId = `${sliceInfo.label}-slice`;
    const textNodeId = `${sliceInfo.label}-slice-label`;

    // The only way to make this keyboard accessible is to have a link
    const interactiveNode = document.createElementNS(SVG_NS, "a");
    interactiveNode.setAttribute("id", interactiveNodeId);
    interactiveNode.setAttribute("xlink:href", `#${interactiveNodeId}`);
    interactiveNode.setAttribute("tabindex", `0`);
    interactiveNode.setAttribute("role", `button`);
    interactiveNode.classList.add("pie-chart-slice-container");
    if (!isPlaceholder) {
      interactiveNode.setAttribute(
        "aria-label",
        L10N.getFormatStr(
          "pieChart.sliceAriaLabel",
          sliceInfo.label,
          new Intl.NumberFormat(undefined, {
            style: "unit",
            unit: "percent",
            maximumFractionDigits: 2,
          }).format((sliceInfo.size / total) * 100)
        )
      );
    }

    sliceNode.append(interactiveNode);

    endAngle = startAngle - sliceAngle;
    midAngle = (startAngle + endAngle) / 2;

    const x1 = centerX + radius * Math.sin(startAngle);
    const y1 = centerY - radius * Math.cos(startAngle);
    const x2 = centerX + radius * Math.sin(endAngle);
    const y2 = centerY - radius * Math.cos(endAngle);
    const largeArcFlag = Math.abs(startAngle - endAngle) > PI ? 1 : 0;

    const pathNode = document.createElementNS(SVG_NS, "path");
    pathNode.classList.add("pie-chart-slice");
    pathNode.setAttribute("data-statistic-name", sliceInfo.label);
    pathNode.setAttribute(
      "d",
      " M " +
        centerX +
        "," +
        centerY +
        " L " +
        x2 +
        "," +
        y2 +
        " A " +
        radius +
        "," +
        radius +
        " 0 " +
        largeArcFlag +
        " 1 " +
        x1 +
        "," +
        y1 +
        " Z"
    );

    if (sliceInfo == largest) {
      pathNode.setAttribute("largest", "");
    }
    if (sliceInfo == smallest) {
      pathNode.setAttribute("smallest", "");
    }

    const hoverX = translateDistance * Math.sin(midAngle);
    const hoverY = -translateDistance * Math.cos(midAngle);
    const hoverTranslate = "translate(" + hoverX + "px, " + hoverY + "px)";
    if (data.length > 1) {
      pathNode.style.transform = hoverTranslate;
    }

    proxy.slices.set(sliceInfo, pathNode);
    delegate(
      proxy,
      ["click", "mouseover", "mouseout", "focus"],
      interactiveNode,
      sliceInfo
    );
    interactiveNode.appendChild(pathNode);

    const textX = centerX + textDistance * Math.sin(midAngle);
    const textY = centerY - textDistance * Math.cos(midAngle);

    // Don't add the label if the slice isn't large enough so it doesn't look cramped.
    if (sliceAngle >= NAMED_SLICE_MIN_ANGLE) {
      const label = document.createElementNS(SVG_NS, "text");
      label.appendChild(document.createTextNode(sliceInfo.label));
      label.setAttribute("id", textNodeId);
      // A label is already set on `interactiveNode`, so hide this from the accessibility tree
      // to avoid duplicating text.
      label.setAttribute("aria-hidden", "true");
      label.setAttribute("class", "pie-chart-label");
      if (data.length > 1) {
        label.style.transform = hoverTranslate;
      }
      label.setAttribute("x", data.length > 1 ? textX : centerX);
      label.setAttribute("y", data.length > 1 ? textY : centerY);
      interactiveNode.append(label);
    }

    startAngle = endAngle;
  }

  return proxy;
}

/**
 * Creates the DOM for a table chart based on the specified properties.
 *
 * @param Document document
 *        The document responsible with creating the DOM.
 * @param object
 *        An object containing all or some of the following properties:
 *          - title: a string displayed as the chart's (description)/local
 *          - data: an array of items used to display each row; all the items
 *                  should be objects representing columns, for which the
 *                  properties' values will be displayed in each cell of a row.
 *                  e.g: [{
 *                    label1: 1,
 *                    label2: 3,
 *                    label3: "foo"
 *                  }, {
 *                    label1: 4,
 *                    label2: 6,
 *                    label3: "bar
 *                  }];
 *          - strings: an object specifying for which rows in the `data` array
 *                     their cell values should be stringified and localized
 *                     based on a predicate function;
 *                     e.g: {
 *                       label1: value => l10n.getFormatStr("...", value)
 *                     }
 *          - totals: an object specifying for which rows in the `data` array
 *                    the sum of their cells is to be displayed in the chart;
 *                    e.g: {
 *                      label1: total => l10n.getFormatStr("...", total),  // 5
 *                      label2: total => l10n.getFormatStr("...", total),  // 9
 *                    }
 *          - header: an object specifying strings to use for table column
 *                    headers
 *                    e.g. {
 *                      label1: l10n.getStr(...),
 *                      label2: l10n.getStr(...),
 *                    }
 * @return TableChart
 *         A table chart proxy instance, which emits the following events:
 *           - "mouseover", when the mouse enters a row
 *           - "mouseout", when the mouse leaves a row
 *           - "click", when the mouse clicks a row
 */
function createTableChart(document, { title, data, strings, totals, header }) {
  strings = strings || {};
  totals = totals || {};
  header = header || {};
  let isPlaceholder = false;

  // If there's no data available, display an empty placeholder.
  if (!data) {
    data = loadingTableChartData();
    isPlaceholder = true;
  }
  if (!data.length) {
    data = emptyTableChartData();
    isPlaceholder = true;
  }

  const container = document.createElement("div");
  container.className = "generic-chart-container table-chart-container";
  container.setAttribute("placeholder", isPlaceholder);

  const proxy = new TableChart(container);

  const titleNode = document.createElement("span");
  titleNode.className = "table-chart-title";
  titleNode.textContent = title;
  container.appendChild(titleNode);

  const tableNode = document.createElement("table");
  tableNode.className = "table-chart-grid";
  container.appendChild(tableNode);

  const headerNode = document.createElement("thead");
  headerNode.className = "table-chart-row";

  const bodyNode = document.createElement("tbody");

  const headerBoxNode = document.createElement("tr");
  headerBoxNode.className = "table-chart-row-box";
  headerNode.appendChild(headerBoxNode);

  for (const [key, value] of Object.entries(header)) {
    const headerLabelNode = document.createElement("th");
    headerLabelNode.className = "table-chart-row-label";
    headerLabelNode.setAttribute("name", key);
    headerLabelNode.textContent = value;
    if (key == "count") {
      headerLabelNode.classList.add("offscreen");
    }
    headerBoxNode.appendChild(headerLabelNode);
  }

  tableNode.append(headerNode, bodyNode);

  for (const rowInfo of data) {
    const rowNode = document.createElement("tr");
    rowNode.className = "table-chart-row";
    rowNode.setAttribute("data-statistic-name", rowInfo.label);

    for (const [key, value] of Object.entries(rowInfo)) {
      // Don't render the "cached" column. We only have it in here so it can be displayed
      // in the `totals` section.
      if (key == "cached") {
        continue;
      }
      const index = data.indexOf(rowInfo);
      const stringified = strings[key] ? strings[key](value, index) : value;
      const labelNode = document.createElement("td");
      labelNode.className = "table-chart-row-label";
      labelNode.setAttribute("name", key);
      labelNode.textContent = stringified;
      rowNode.appendChild(labelNode);
    }

    proxy.rows.set(rowInfo, rowNode);
    delegate(proxy, ["click", "mouseover", "mouseout"], rowNode, rowInfo);
    bodyNode.appendChild(rowNode);
  }

  const totalsNode = document.createElement("div");
  totalsNode.className = "table-chart-totals";

  for (const [key, value] of Object.entries(totals)) {
    const total = data.reduce((acc, e) => acc + e[key], 0);
    const stringified = value ? value(total || 0) : total;
    const labelNode = document.createElement("span");
    labelNode.className = "table-chart-summary-label";
    labelNode.setAttribute("name", key);
    labelNode.textContent = stringified;
    totalsNode.appendChild(labelNode);
  }

  container.appendChild(totalsNode);

  return proxy;
}

function loadingPieChartData() {
  return [{ size: 1, label: L10N.getStr("pieChart.loading") }];
}

function emptyPieChartData() {
  return [{ size: 1, label: L10N.getStr("pieChart.unavailable") }];
}

function loadingTableChartData() {
  return [{ size: "", label: L10N.getStr("tableChart.loading") }];
}

function emptyTableChartData() {
  return [{ size: "", label: L10N.getStr("tableChart.unavailable") }];
}

/**
 * Delegates DOM events emitted by a Node to an EventEmitter proxy.
 *
 * @param EventEmitter emitter
 *        The event emitter proxy instance.
 * @param array events
 *        An array of events, e.g. ["mouseover", "mouseout"].
 * @param Node node
 *        The element firing the DOM events.
 * @param any args
 *        The arguments passed when emitting events through the proxy.
 */
function delegate(emitter, events, node, args) {
  for (const event of events) {
    node.addEventListener(event, emitter.emit.bind(emitter, event, args));
  }
}

exports.Chart = Chart;

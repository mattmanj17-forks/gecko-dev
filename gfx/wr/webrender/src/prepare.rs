/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! # Prepare pass
//!
//! TODO: document this!

use api::{ColorF, DebugFlags, PropertyBinding};
use api::{BoxShadowClipMode, BorderStyle, ClipMode};
use api::units::*;
use euclid::Scale;
use smallvec::SmallVec;
use crate::composite::CompositorSurfaceKind;
use crate::command_buffer::{CommandBufferIndex, PrimitiveCommand};
use crate::image_tiling::{self, Repetition};
use crate::border::{get_max_scale_for_border, build_border_instances};
use crate::clip::{ClipStore, ClipNodeRange};
use crate::pattern::Pattern;
use crate::spatial_tree::{SpatialNodeIndex, SpatialTree};
use crate::clip::{ClipDataStore, ClipNodeFlags, ClipChainInstance, ClipItemKind};
use crate::frame_builder::{FrameBuildingContext, FrameBuildingState, PictureContext, PictureState};
use crate::gpu_cache::{GpuCacheHandle, GpuDataRequest};
use crate::gpu_types::BrushFlags;
use crate::internal_types::{FastHashMap, PlaneSplitAnchor, Filter};
use crate::picture::{ClusterFlags, PictureCompositeMode, PicturePrimitive, SliceId};
use crate::picture::{PrimitiveList, PrimitiveCluster, SurfaceIndex, TileCacheInstance, SubpixelMode, Picture3DContext};
use crate::prim_store::line_dec::MAX_LINE_DECORATION_RESOLUTION;
use crate::prim_store::*;
use crate::quad;
use crate::prim_store::gradient::GradientGpuBlockBuilder;
use crate::render_backend::DataStores;
use crate::render_task_graph::RenderTaskId;
use crate::render_task_cache::RenderTaskCacheKeyKind;
use crate::render_task_cache::{RenderTaskCacheKey, to_cache_size, RenderTaskParent};
use crate::render_task::{EmptyTask, MaskSubPass, RenderTask, RenderTaskKind, SubPass};
use crate::segment::SegmentBuilder;
use crate::util::{clamp_to_scale_factor, pack_as_float, ScaleOffset};
use crate::visibility::{compute_conservative_visible_rect, PrimitiveVisibility, VisibilityState};


const MAX_MASK_SIZE: i32 = 4096;

const MIN_BRUSH_SPLIT_AREA: f32 = 128.0 * 128.0;

/// The entry point of the preapre pass.
pub fn prepare_picture(
    pic_index: PictureIndex,
    store: &mut PrimitiveStore,
    surface_index: Option<SurfaceIndex>,
    subpixel_mode: SubpixelMode,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    data_stores: &mut DataStores,
    scratch: &mut PrimitiveScratchBuffer,
    tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
    prim_instances: &mut Vec<PrimitiveInstance>,
) -> bool {
    if frame_state.visited_pictures[pic_index.0] {
        return true;
    }

    frame_state.visited_pictures[pic_index.0] = true;

    let pic = &mut store.pictures[pic_index.0];
    let Some((pic_context, mut pic_state, mut prim_list)) = pic.take_context(
        pic_index,
        surface_index,
        subpixel_mode,
        frame_state,
        frame_context,
        data_stores,
        scratch,
        tile_caches,
    ) else {
        return false;
    };

    prepare_primitives(
        store,
        &mut prim_list,
        &pic_context,
        &mut pic_state,
        frame_context,
        frame_state,
        data_stores,
        scratch,
        tile_caches,
        prim_instances,
    );

    // Restore the dependencies (borrow check dance)
    store.pictures[pic_context.pic_index.0].restore_context(
        pic_context.pic_index,
        prim_list,
        pic_context,
        prim_instances,
        frame_context,
        frame_state,
    );

    true
}

fn prepare_primitives(
    store: &mut PrimitiveStore,
    prim_list: &mut PrimitiveList,
    pic_context: &PictureContext,
    pic_state: &mut PictureState,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    data_stores: &mut DataStores,
    scratch: &mut PrimitiveScratchBuffer,
    tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
    prim_instances: &mut Vec<PrimitiveInstance>,
) {
    profile_scope!("prepare_primitives");
    let mut cmd_buffer_targets = Vec::new();

    for cluster in &mut prim_list.clusters {
        if !cluster.flags.contains(ClusterFlags::IS_VISIBLE) {
            continue;
        }
        profile_scope!("cluster");
        pic_state.map_local_to_pic.set_target_spatial_node(
            cluster.spatial_node_index,
            frame_context.spatial_tree,
        );

        for prim_instance_index in cluster.prim_range() {
            if frame_state.surface_builder.get_cmd_buffer_targets_for_prim(
                &prim_instances[prim_instance_index].vis,
                &mut cmd_buffer_targets,
            ) {
                let plane_split_anchor = PlaneSplitAnchor::new(
                    cluster.spatial_node_index,
                    PrimitiveInstanceIndex(prim_instance_index as u32),
                );

                prepare_prim_for_render(
                    store,
                    prim_instance_index,
                    cluster,
                    pic_context,
                    pic_state,
                    frame_context,
                    frame_state,
                    plane_split_anchor,
                    data_stores,
                    scratch,
                    tile_caches,
                    prim_instances,
                    &cmd_buffer_targets,
                );

                frame_state.num_visible_primitives += 1;
                continue;
            }

            // TODO(gw): Technically no need to clear visibility here, since from this point it
            //           only matters if it got added to a command buffer. Kept here for now to
            //           make debugging simpler, but perhaps we can remove / tidy this up.
            prim_instances[prim_instance_index].clear_visibility();
        }
    }
}

fn can_use_clip_chain_for_quad_path(
    clip_chain: &ClipChainInstance,
    clip_store: &ClipStore,
    data_stores: &DataStores,
) -> bool {
    if !clip_chain.needs_mask {
        return true;
    }

    for i in 0 .. clip_chain.clips_range.count {
        let clip_instance = clip_store.get_instance_from_range(&clip_chain.clips_range, i);
        let clip_node = &data_stores.clip[clip_instance.handle];

        match clip_node.item.kind {
            ClipItemKind::RoundedRectangle { .. } | ClipItemKind::Rectangle { .. } => {}
            ClipItemKind::BoxShadow { .. } => {
                // legacy path for box-shadows for now (move them to a separate primitive next)
                return false;
            }
            ClipItemKind::Image { .. } => {
                panic!("bug: image-masks not expected on rect/quads");
            }
        }
    }

    true
}

fn prepare_prim_for_render(
    store: &mut PrimitiveStore,
    prim_instance_index: usize,
    cluster: &mut PrimitiveCluster,
    pic_context: &PictureContext,
    pic_state: &mut PictureState,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    plane_split_anchor: PlaneSplitAnchor,
    data_stores: &mut DataStores,
    scratch: &mut PrimitiveScratchBuffer,
    tile_caches: &mut FastHashMap<SliceId, Box<TileCacheInstance>>,
    prim_instances: &mut Vec<PrimitiveInstance>,
    targets: &[CommandBufferIndex],
) {
    profile_scope!("prepare_prim_for_render");

    // If we have dependencies, we need to prepare them first, in order
    // to know the actual rect of this primitive.
    // For example, scrolling may affect the location of an item in
    // local space, which may force us to render this item on a larger
    // picture target, if being composited.
    let mut is_passthrough = false;
    if let PrimitiveInstanceKind::Picture { pic_index, .. } = prim_instances[prim_instance_index].kind {
        if !prepare_picture(
            pic_index,
            store,
            Some(pic_context.surface_index),
            pic_context.subpixel_mode,
            frame_context,
            frame_state,
            data_stores,
            scratch,
            tile_caches,
            prim_instances
        ) {
            return;
        }

        is_passthrough = store
            .pictures[pic_index.0]
            .composite_mode
            .is_none();
    }

    let prim_instance = &mut prim_instances[prim_instance_index];

    if !is_passthrough {
        fn may_need_repetition(stretch_size: LayoutSize, prim_rect: LayoutRect) -> bool {
            stretch_size.width < prim_rect.width() ||
                stretch_size.height < prim_rect.height()
        }
        // Bug 1887841: At the moment the quad shader does not support repetitions.
        // Bug 1888349: Some primitives have brush segments that aren't handled by
        // the quad infrastructure yet.
        let disable_quad_path = match &prim_instance.kind {
            PrimitiveInstanceKind::Rectangle { .. } => false,
            PrimitiveInstanceKind::LinearGradient { data_handle, .. } => {
                let prim_data = &data_stores.linear_grad[*data_handle];
                !prim_data.brush_segments.is_empty() ||
                    may_need_repetition(prim_data.stretch_size, prim_data.common.prim_rect)
            }
            PrimitiveInstanceKind::RadialGradient { data_handle, .. } => {
                let prim_data = &data_stores.radial_grad[*data_handle];
                !prim_data.brush_segments.is_empty() ||
                    may_need_repetition(prim_data.stretch_size, prim_data.common.prim_rect)
            }
            // TODO(bug 1899546) Enable quad conic gradients with SWGL.
            PrimitiveInstanceKind::ConicGradient { data_handle, .. } if !frame_context.fb_config.is_software => {
                let prim_data = &data_stores.conic_grad[*data_handle];
                !prim_data.brush_segments.is_empty() ||
                    may_need_repetition(prim_data.stretch_size, prim_data.common.prim_rect)
            }
            _ => true,
        };

        // In this initial patch, we only support non-masked primitives through the new
        // quad rendering path. Follow up patches will extend this to support masks, and
        // then use by other primitives. In the new quad rendering path, we'll still want
        // to skip the entry point to `update_clip_task` as that does old-style segmenting
        // and mask generation.
        let should_update_clip_task = match prim_instance.kind {
            PrimitiveInstanceKind::Rectangle { use_legacy_path: ref mut no_quads, .. }
            | PrimitiveInstanceKind::RadialGradient { cached: ref mut no_quads, .. }
            | PrimitiveInstanceKind::ConicGradient { cached: ref mut no_quads, .. }
            => {
                *no_quads = disable_quad_path || !can_use_clip_chain_for_quad_path(
                    &prim_instance.vis.clip_chain,
                    frame_state.clip_store,
                    data_stores,
                );

                *no_quads
            }
            PrimitiveInstanceKind::BoxShadow { .. } |
            PrimitiveInstanceKind::Picture { .. } => false,
            _ => true,
        };

        if should_update_clip_task {
            let prim_rect = data_stores.get_local_prim_rect(
                prim_instance,
                &store.pictures,
                frame_state.surfaces,
            );

            if !update_clip_task(
                prim_instance,
                &prim_rect.min,
                cluster.spatial_node_index,
                pic_context.raster_spatial_node_index,
                pic_context.visibility_spatial_node_index,
                pic_context,
                pic_state,
                frame_context,
                frame_state,
                store,
                data_stores,
                scratch,
            ) {
                return;
            }
        }
    }

    prepare_interned_prim_for_render(
        store,
        PrimitiveInstanceIndex(prim_instance_index as u32),
        prim_instance,
        cluster,
        plane_split_anchor,
        pic_context,
        pic_state,
        frame_context,
        frame_state,
        data_stores,
        scratch,
        targets,
    )
}

/// Prepare an interned primitive for rendering, by requesting
/// resources, render tasks etc. This is equivalent to the
/// prepare_prim_for_render_inner call for old style primitives.
fn prepare_interned_prim_for_render(
    store: &mut PrimitiveStore,
    prim_instance_index: PrimitiveInstanceIndex,
    prim_instance: &mut PrimitiveInstance,
    cluster: &mut PrimitiveCluster,
    plane_split_anchor: PlaneSplitAnchor,
    pic_context: &PictureContext,
    pic_state: &mut PictureState,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    data_stores: &mut DataStores,
    scratch: &mut PrimitiveScratchBuffer,
    targets: &[CommandBufferIndex],
) {
    let prim_spatial_node_index = cluster.spatial_node_index;
    let device_pixel_scale = frame_state.surfaces[pic_context.surface_index.0].device_pixel_scale;

    match &mut prim_instance.kind {
        PrimitiveInstanceKind::BoxShadow { data_handle } => {
            let prim_data = &mut data_stores.box_shadow[*data_handle];

            quad::prepare_quad(
                prim_data,
                &prim_data.kind.outer_shadow_rect,
                prim_instance_index,
                prim_spatial_node_index,
                &prim_instance.vis.clip_chain,
                device_pixel_scale,
                frame_context,
                pic_context,
                targets,
                &data_stores.clip,
                frame_state,
                pic_state,
                scratch,
            );

            return;
        }
        PrimitiveInstanceKind::LineDecoration { data_handle, ref mut render_task, .. } => {
            profile_scope!("LineDecoration");
            let prim_data = &mut data_stores.line_decoration[*data_handle];
            let common_data = &mut prim_data.common;
            let line_dec_data = &mut prim_data.kind;

            // Update the template this instane references, which may refresh the GPU
            // cache with any shared template data.
            line_dec_data.update(common_data, frame_state);

            // Work out the device pixel size to be used to cache this line decoration.

            // If we have a cache key, it's a wavy / dashed / dotted line. Otherwise, it's
            // a simple solid line.
            if let Some(cache_key) = line_dec_data.cache_key.as_ref() {
                // TODO(gw): These scale factors don't do a great job if the world transform
                //           contains perspective
                let scale = frame_context
                    .spatial_tree
                    .get_world_transform(prim_spatial_node_index)
                    .scale_factors();

                // Scale factors are normalized to a power of 2 to reduce the number of
                // resolution changes.
                // For frames with a changing scale transform round scale factors up to
                // nearest power-of-2 boundary so that we don't keep having to redraw
                // the content as it scales up and down. Rounding up to nearest
                // power-of-2 boundary ensures we never scale up, only down --- avoiding
                // jaggies. It also ensures we never scale down by more than a factor of
                // 2, avoiding bad downscaling quality.
                let scale_width = clamp_to_scale_factor(scale.0, false);
                let scale_height = clamp_to_scale_factor(scale.1, false);
                // Pick the maximum dimension as scale
                let world_scale = LayoutToWorldScale::new(scale_width.max(scale_height));

                let scale_factor = world_scale * Scale::new(1.0);
                let task_size_f = (LayoutSize::from_au(cache_key.size) * scale_factor).ceil();
                let mut task_size = if task_size_f.width > MAX_LINE_DECORATION_RESOLUTION as f32 ||
                   task_size_f.height > MAX_LINE_DECORATION_RESOLUTION as f32 {
                     let max_extent = task_size_f.width.max(task_size_f.height);
                     let task_scale_factor = Scale::new(MAX_LINE_DECORATION_RESOLUTION as f32 / max_extent);
                     let task_size = (LayoutSize::from_au(cache_key.size) * scale_factor * task_scale_factor)
                                    .ceil().to_i32();
                    task_size
                } else {
                    task_size_f.to_i32()
                };

                // It's plausible, due to float accuracy issues that the line decoration may be considered
                // visible even if the scale factors are ~0. However, the render task allocation below requires
                // that the size of the task is > 0. To work around this, ensure that the task size is at least
                // 1x1 pixels
                task_size.width = task_size.width.max(1);
                task_size.height = task_size.height.max(1);

                // Request a pre-rendered image task.
                // TODO(gw): This match is a bit untidy, but it should disappear completely
                //           once the prepare_prims and batching are unified. When that
                //           happens, we can use the cache handle immediately, and not need
                //           to temporarily store it in the primitive instance.
                *render_task = Some(frame_state.resource_cache.request_render_task(
                    Some(RenderTaskCacheKey {
                        size: task_size,
                        kind: RenderTaskCacheKeyKind::LineDecoration(cache_key.clone()),
                    }),
                    false,
                    RenderTaskParent::Surface,
                    frame_state.gpu_cache,
                    &mut frame_state.frame_gpu_data.f32,
                    frame_state.rg_builder,
                    &mut frame_state.surface_builder,
                    &mut |rg_builder, _, _| {
                        rg_builder.add().init(RenderTask::new_dynamic(
                            task_size,
                            RenderTaskKind::new_line_decoration(
                                cache_key.style,
                                cache_key.orientation,
                                cache_key.wavy_line_thickness.to_f32_px(),
                                LayoutSize::from_au(cache_key.size),
                            ),
                        ))
                    }
                ));
            }
        }
        PrimitiveInstanceKind::TextRun { run_index, data_handle, .. } => {
            profile_scope!("TextRun");
            let prim_data = &mut data_stores.text_run[*data_handle];
            let run = &mut store.text_runs[*run_index];

            prim_data.common.may_need_repetition = false;

            // The glyph transform has to match `glyph_transform` in "ps_text_run" shader.
            // It's relative to the rasterizing space of a glyph.
            let transform = frame_context.spatial_tree
                .get_relative_transform(
                    prim_spatial_node_index,
                    pic_context.raster_spatial_node_index,
                )
                .into_fast_transform();
            let prim_offset = prim_data.common.prim_rect.min.to_vector() - run.reference_frame_relative_offset;

            let surface = &frame_state.surfaces[pic_context.surface_index.0];

            // If subpixel AA is disabled due to the backing surface the glyphs
            // are being drawn onto, disable it (unless we are using the
            // specifial subpixel mode that estimates background color).
            let allow_subpixel = match prim_instance.vis.state {
                VisibilityState::Culled |
                VisibilityState::Unset |
                VisibilityState::PassThrough => {
                    panic!("bug: invalid visibility state");
                }
                VisibilityState::Visible { sub_slice_index, .. } => {
                    // For now, we only allow subpixel AA on primary sub-slices. In future we
                    // may support other sub-slices if we find content that does this.
                    if sub_slice_index.is_primary() {
                        match pic_context.subpixel_mode {
                            SubpixelMode::Allow => true,
                            SubpixelMode::Deny => false,
                            SubpixelMode::Conditional { allowed_rect, prohibited_rect } => {
                                // Conditional mode allows subpixel AA to be enabled for this
                                // text run, so long as it's inside the allowed rect.
                                allowed_rect.contains_box(&prim_instance.vis.clip_chain.pic_coverage_rect) &&
                                !prohibited_rect.intersects(&prim_instance.vis.clip_chain.pic_coverage_rect)
                            }
                        }
                    } else {
                        false
                    }
                }
            };

            run.request_resources(
                prim_offset,
                &prim_data.font,
                &prim_data.glyphs,
                &transform.to_transform().with_destination::<_>(),
                surface,
                prim_spatial_node_index,
                allow_subpixel,
                frame_context.fb_config.low_quality_pinch_zoom,
                frame_state.resource_cache,
                frame_state.gpu_cache,
                frame_context.spatial_tree,
                scratch,
            );

            // Update the template this instane references, which may refresh the GPU
            // cache with any shared template data.
            prim_data.update(frame_state);
        }
        PrimitiveInstanceKind::Clear { data_handle, .. } => {
            profile_scope!("Clear");
            let prim_data = &mut data_stores.prim[*data_handle];

            prim_data.common.may_need_repetition = false;

            // Update the template this instane references, which may refresh the GPU
            // cache with any shared template data.
            prim_data.update(frame_state, frame_context.scene_properties);
        }
        PrimitiveInstanceKind::NormalBorder { data_handle, ref mut render_task_ids, .. } => {
            profile_scope!("NormalBorder");
            let prim_data = &mut data_stores.normal_border[*data_handle];
            let common_data = &mut prim_data.common;
            let border_data = &mut prim_data.kind;

            common_data.may_need_repetition =
                matches!(border_data.border.top.style, BorderStyle::Dotted | BorderStyle::Dashed) ||
                matches!(border_data.border.right.style, BorderStyle::Dotted | BorderStyle::Dashed) ||
                matches!(border_data.border.bottom.style, BorderStyle::Dotted | BorderStyle::Dashed) ||
                matches!(border_data.border.left.style, BorderStyle::Dotted | BorderStyle::Dashed);


            // Update the template this instance references, which may refresh the GPU
            // cache with any shared template data.
            border_data.update(common_data, frame_state);

            // TODO(gw): For now, the scale factors to rasterize borders at are
            //           based on the true world transform of the primitive. When
            //           raster roots with local scale are supported in future,
            //           that will need to be accounted for here.
            let scale = frame_context
                .spatial_tree
                .get_world_transform(prim_spatial_node_index)
                .scale_factors();

            // Scale factors are normalized to a power of 2 to reduce the number of
            // resolution changes.
            // For frames with a changing scale transform round scale factors up to
            // nearest power-of-2 boundary so that we don't keep having to redraw
            // the content as it scales up and down. Rounding up to nearest
            // power-of-2 boundary ensures we never scale up, only down --- avoiding
            // jaggies. It also ensures we never scale down by more than a factor of
            // 2, avoiding bad downscaling quality.
            let scale_width = clamp_to_scale_factor(scale.0, false);
            let scale_height = clamp_to_scale_factor(scale.1, false);
            // Pick the maximum dimension as scale
            let world_scale = LayoutToWorldScale::new(scale_width.max(scale_height));
            let mut scale = world_scale * device_pixel_scale;
            let max_scale = get_max_scale_for_border(border_data);
            scale.0 = scale.0.min(max_scale.0);

            // For each edge and corner, request the render task by content key
            // from the render task cache. This ensures that the render task for
            // this segment will be available for batching later in the frame.
            let mut handles: SmallVec<[RenderTaskId; 8]> = SmallVec::new();

            for segment in &border_data.border_segments {
                // Update the cache key device size based on requested scale.
                let cache_size = to_cache_size(segment.local_task_size, &mut scale);
                let cache_key = RenderTaskCacheKey {
                    kind: RenderTaskCacheKeyKind::BorderSegment(segment.cache_key.clone()),
                    size: cache_size,
                };

                handles.push(frame_state.resource_cache.request_render_task(
                    Some(cache_key),
                    false,          // TODO(gw): We don't calculate opacity for borders yet!
                    RenderTaskParent::Surface,
                    frame_state.gpu_cache,
                    &mut frame_state.frame_gpu_data.f32,
                    frame_state.rg_builder,
                    &mut frame_state.surface_builder,
                    &mut |rg_builder, _, _| {
                        rg_builder.add().init(RenderTask::new_dynamic(
                            cache_size,
                            RenderTaskKind::new_border_segment(
                                build_border_instances(
                                    &segment.cache_key,
                                    cache_size,
                                    &border_data.border,
                                    scale,
                                )
                            ),
                        ))
                    }
                ));
            }

            *render_task_ids = scratch
                .border_cache_handles
                .extend(handles);
        }
        PrimitiveInstanceKind::ImageBorder { data_handle, .. } => {
            profile_scope!("ImageBorder");
            let prim_data = &mut data_stores.image_border[*data_handle];

            // TODO: get access to the ninepatch and to check whether we need support
            // for repetitions in the shader.

            // Update the template this instance references, which may refresh the GPU
            // cache with any shared template data.
            prim_data.kind.update(
                &mut prim_data.common,
                frame_state
            );
        }
        PrimitiveInstanceKind::Rectangle { data_handle, segment_instance_index, color_binding_index, use_legacy_path, .. } => {
            profile_scope!("Rectangle");

            if *use_legacy_path {
                let prim_data = &mut data_stores.prim[*data_handle];
                prim_data.common.may_need_repetition = false;

                // TODO(gw): Legacy rect rendering path - remove once we support masks on quad prims
                if *color_binding_index != ColorBindingIndex::INVALID {
                    match store.color_bindings[*color_binding_index] {
                        PropertyBinding::Binding(..) => {
                            // We explicitly invalidate the gpu cache
                            // if the color is animating.
                            let gpu_cache_handle =
                                if *segment_instance_index == SegmentInstanceIndex::INVALID {
                                    None
                                } else if *segment_instance_index == SegmentInstanceIndex::UNUSED {
                                    Some(&prim_data.common.gpu_cache_handle)
                                } else {
                                    Some(&scratch.segment_instances[*segment_instance_index].gpu_cache_handle)
                                };
                            if let Some(gpu_cache_handle) = gpu_cache_handle {
                                frame_state.gpu_cache.invalidate(gpu_cache_handle);
                            }
                        }
                        PropertyBinding::Value(..) => {},
                    }
                }

                // Update the template this instane references, which may refresh the GPU
                // cache with any shared template data.
                prim_data.update(
                    frame_state,
                    frame_context.scene_properties,
                );

                write_segment(
                    *segment_instance_index,
                    frame_state,
                    &mut scratch.segments,
                    &mut scratch.segment_instances,
                    |request| {
                        prim_data.kind.write_prim_gpu_blocks(
                            request,
                            frame_context.scene_properties,
                        );
                    }
                );
            } else {
                let prim_data = &data_stores.prim[*data_handle];

                quad::prepare_quad(
                    prim_data,
                    &prim_data.common.prim_rect,
                    prim_instance_index,
                    prim_spatial_node_index,
                    &prim_instance.vis.clip_chain,
                    device_pixel_scale,
                    frame_context,
                    pic_context,
                    targets,
                    &data_stores.clip,
                    frame_state,
                    pic_state,
                    scratch,
                );

                return;
            }
        }
        PrimitiveInstanceKind::YuvImage { data_handle, segment_instance_index, .. } => {
            profile_scope!("YuvImage");
            let prim_data = &mut data_stores.yuv_image[*data_handle];
            let common_data = &mut prim_data.common;
            let yuv_image_data = &mut prim_data.kind;

            common_data.may_need_repetition = false;

            // Update the template this instane references, which may refresh the GPU
            // cache with any shared template data.
            yuv_image_data.update(common_data, frame_state);

            write_segment(
                *segment_instance_index,
                frame_state,
                &mut scratch.segments,
                &mut scratch.segment_instances,
                |request| {
                    yuv_image_data.write_prim_gpu_blocks(request);
                }
            );
        }
        PrimitiveInstanceKind::Image { data_handle, image_instance_index, .. } => {
            profile_scope!("Image");

            let prim_data = &mut data_stores.image[*data_handle];
            let common_data = &mut prim_data.common;
            let image_data = &mut prim_data.kind;
            let image_instance = &mut store.images[*image_instance_index];

            // Update the template this instance references, which may refresh the GPU
            // cache with any shared template data.
            image_data.update(
                common_data,
                image_instance,
                prim_spatial_node_index,
                frame_state,
                frame_context,
                &mut prim_instance.vis,
            );

            write_segment(
                image_instance.segment_instance_index,
                frame_state,
                &mut scratch.segments,
                &mut scratch.segment_instances,
                |request| {
                    image_data.write_prim_gpu_blocks(&image_instance.adjustment, request);
                },
            );
        }
        PrimitiveInstanceKind::LinearGradient { data_handle, ref mut visible_tiles_range, .. } => {
            profile_scope!("LinearGradient");
            let prim_data = &mut data_stores.linear_grad[*data_handle];

            // Update the template this instane references, which may refresh the GPU
            // cache with any shared template data.
            prim_data.update(frame_state);

            if prim_data.stretch_size.width >= prim_data.common.prim_rect.width() &&
                prim_data.stretch_size.height >= prim_data.common.prim_rect.height() {

                prim_data.common.may_need_repetition = false;
            }

            if prim_data.tile_spacing != LayoutSize::zero() {
                // We are performing the decomposition on the CPU here, no need to
                // have it in the shader.
                prim_data.common.may_need_repetition = false;

                *visible_tiles_range = decompose_repeated_gradient(
                    &prim_instance.vis,
                    &prim_data.common.prim_rect,
                    prim_spatial_node_index,
                    &prim_data.stretch_size,
                    &prim_data.tile_spacing,
                    frame_state,
                    &mut scratch.gradient_tiles,
                    &frame_context.spatial_tree,
                    Some(&mut |_, mut request| {
                        request.push([
                            prim_data.start_point.x,
                            prim_data.start_point.y,
                            prim_data.end_point.x,
                            prim_data.end_point.y,
                        ]);
                        request.push([
                            pack_as_float(prim_data.extend_mode as u32),
                            prim_data.stretch_size.width,
                            prim_data.stretch_size.height,
                            0.0,
                        ]);
                    }),
                );

                if visible_tiles_range.is_empty() {
                    prim_instance.clear_visibility();
                }
            }

            let stops_address = GradientGpuBlockBuilder::build(
                prim_data.reverse_stops,
                &mut frame_state.frame_gpu_data.f32,
                &prim_data.stops,
            );

            // TODO(gw): Consider whether it's worth doing segment building
            //           for gradient primitives.
            frame_state.push_prim(
                &PrimitiveCommand::instance(prim_instance_index, stops_address),
                prim_spatial_node_index,
                targets,
            );
            return;
        }
        PrimitiveInstanceKind::CachedLinearGradient { data_handle, ref mut visible_tiles_range, .. } => {
            profile_scope!("CachedLinearGradient");
            let prim_data = &mut data_stores.linear_grad[*data_handle];
            prim_data.common.may_need_repetition = prim_data.stretch_size.width < prim_data.common.prim_rect.width()
                || prim_data.stretch_size.height < prim_data.common.prim_rect.height();

            // Update the template this instance references, which may refresh the GPU
            // cache with any shared template data.
            prim_data.update(frame_state);

            if prim_data.tile_spacing != LayoutSize::zero() {
                prim_data.common.may_need_repetition = false;

                *visible_tiles_range = decompose_repeated_gradient(
                    &prim_instance.vis,
                    &prim_data.common.prim_rect,
                    prim_spatial_node_index,
                    &prim_data.stretch_size,
                    &prim_data.tile_spacing,
                    frame_state,
                    &mut scratch.gradient_tiles,
                    &frame_context.spatial_tree,
                    None,
                );

                if visible_tiles_range.is_empty() {
                    prim_instance.clear_visibility();
                }
            }
        }
        PrimitiveInstanceKind::RadialGradient { data_handle, ref mut visible_tiles_range, cached, .. } => {
            profile_scope!("RadialGradient");
            let prim_data = &mut data_stores.radial_grad[*data_handle];

            if !*cached {
                quad::prepare_quad(
                    prim_data,
                    &prim_data.common.prim_rect,
                    prim_instance_index,
                    prim_spatial_node_index,
                    &prim_instance.vis.clip_chain,
                    device_pixel_scale,
                    frame_context,
                    pic_context,
                    targets,
                    &data_stores.clip,
                    frame_state,
                    pic_state,
                    scratch,
                );

                return;
            }

            prim_data.common.may_need_repetition = prim_data.stretch_size.width < prim_data.common.prim_rect.width()
            || prim_data.stretch_size.height < prim_data.common.prim_rect.height();

            // Update the template this instane references, which may refresh the GPU
            // cache with any shared template data.
            prim_data.update(frame_state);

            if prim_data.tile_spacing != LayoutSize::zero() {
                prim_data.common.may_need_repetition = false;

                *visible_tiles_range = decompose_repeated_gradient(
                    &prim_instance.vis,
                    &prim_data.common.prim_rect,
                    prim_spatial_node_index,
                    &prim_data.stretch_size,
                    &prim_data.tile_spacing,
                    frame_state,
                    &mut scratch.gradient_tiles,
                    &frame_context.spatial_tree,
                    None,
                );

                if visible_tiles_range.is_empty() {
                    prim_instance.clear_visibility();
                }
            }
        }
        PrimitiveInstanceKind::ConicGradient { data_handle, ref mut visible_tiles_range, cached, .. } => {
            profile_scope!("ConicGradient");
            let prim_data = &mut data_stores.conic_grad[*data_handle];

            if !*cached {
                quad::prepare_quad(
                    prim_data,
                    &prim_data.common.prim_rect,
                    prim_instance_index,
                    prim_spatial_node_index,
                    &prim_instance.vis.clip_chain,
                    device_pixel_scale,
                    frame_context,
                    pic_context,
                    targets,
                    &data_stores.clip,
                    frame_state,
                    pic_state,
                    scratch,
                );

                return;
            }

            prim_data.common.may_need_repetition = prim_data.stretch_size.width < prim_data.common.prim_rect.width()
                || prim_data.stretch_size.height < prim_data.common.prim_rect.height();

            // Update the template this instane references, which may refresh the GPU
            // cache with any shared template data.
            prim_data.update(frame_state);

            if prim_data.tile_spacing != LayoutSize::zero() {
                prim_data.common.may_need_repetition = false;

                *visible_tiles_range = decompose_repeated_gradient(
                    &prim_instance.vis,
                    &prim_data.common.prim_rect,
                    prim_spatial_node_index,
                    &prim_data.stretch_size,
                    &prim_data.tile_spacing,
                    frame_state,
                    &mut scratch.gradient_tiles,
                    &frame_context.spatial_tree,
                    None,
                );

                if visible_tiles_range.is_empty() {
                    prim_instance.clear_visibility();
                }
            }

            // TODO(gw): Consider whether it's worth doing segment building
            //           for gradient primitives.
        }
        PrimitiveInstanceKind::Picture { pic_index, .. } => {
            profile_scope!("Picture");
            let pic = &mut store.pictures[pic_index.0];

            if prim_instance.vis.clip_chain.needs_mask {
                // TODO(gw): Much of the code in this branch could be moved in to a common
                //           function as we move more primitives to the new clip-mask paths.

                // We are going to split the clip mask tasks in to a list to be rendered
                // on the source picture, and those to be rendered in to a mask for
                // compositing the picture in to the target.
                let mut source_masks = Vec::new();
                let mut target_masks = Vec::new();

                // For some composite modes, we force target mask due to limitations. That
                // might results in artifacts for these modes (which are already an existing
                // problem) but we can handle these cases as follow ups.
                let force_target_mask = match pic.composite_mode {
                    // We can't currently render over top of these filters as their size
                    // may have changed due to downscaling. We could handle this separate
                    // case as a follow up.
                    Some(PictureCompositeMode::Filter(Filter::Blur { .. })) |
                    Some(PictureCompositeMode::Filter(Filter::DropShadows { .. })) |
                    Some(PictureCompositeMode::SVGFEGraph( .. )) => {
                        true
                    }
                    _ => {
                        false
                    }
                };

                // Work out which clips get drawn in to the source / target mask
                for i in 0 .. prim_instance.vis.clip_chain.clips_range.count {
                    let clip_instance = frame_state.clip_store.get_instance_from_range(&prim_instance.vis.clip_chain.clips_range, i);

                    if !force_target_mask && clip_instance.flags.contains(ClipNodeFlags::SAME_COORD_SYSTEM) {
                        source_masks.push(i);
                    } else {
                        target_masks.push(i);
                    }
                }

                let pic_surface_index = pic.raster_config.as_ref().unwrap().surface_index;
                let prim_local_rect = frame_state
                    .surfaces[pic_surface_index.0]
                    .clipped_local_rect
                    .cast_unit();

                let pattern = Pattern::color(ColorF::WHITE);

                let prim_address_f = quad::write_prim_blocks(
                    &mut frame_state.frame_gpu_data.f32,
                    prim_local_rect,
                    prim_instance.vis.clip_chain.local_clip_rect,
                    pattern.base_color,
                    pattern.texture_input.task_id,
                    &[],
                    ScaleOffset::identity(),
                );

                // Handle masks on the source. This is the common case, and occurs for:
                // (a) Any masks in the same coord space as the surface
                // (b) All masks if the surface and parent are axis-aligned
                if !source_masks.is_empty() {
                    let first_clip_node_index = frame_state.clip_store.clip_node_instances.len() as u32;
                    let parent_task_id = pic.primary_render_task_id.expect("bug: no composite mode");

                    // Construct a new clip node range, also add image-mask dependencies as needed
                    for instance in source_masks {
                        let clip_instance = frame_state.clip_store.get_instance_from_range(&prim_instance.vis.clip_chain.clips_range, instance);

                        for tile in frame_state.clip_store.visible_mask_tiles(clip_instance) {
                            frame_state.rg_builder.add_dependency(
                                parent_task_id,
                                tile.task_id,
                            );
                        }

                        frame_state.clip_store.clip_node_instances.push(clip_instance.clone());
                    }

                    let clip_node_range = ClipNodeRange {
                        first: first_clip_node_index,
                        count: frame_state.clip_store.clip_node_instances.len() as u32 - first_clip_node_index,
                    };

                    let masks = MaskSubPass {
                        clip_node_range,
                        prim_spatial_node_index,
                        prim_address_f,
                    };

                    // Add the mask as a sub-pass of the picture
                    let pic_task_id = pic.primary_render_task_id.expect("uh oh");
                    let pic_task = frame_state.rg_builder.get_task_mut(pic_task_id);
                    pic_task.add_sub_pass(SubPass::Masks {
                        masks,
                    });
                }

                // Handle masks on the target. This is the rare case, and occurs for:
                // Masks in parent space when non-axis-aligned to source space
                if !target_masks.is_empty() {
                    let surface = &frame_state.surfaces[pic_context.surface_index.0];
                    let coverage_rect = prim_instance.vis.clip_chain.pic_coverage_rect;

                    let device_pixel_scale = surface.device_pixel_scale;
                    let raster_spatial_node_index = surface.raster_spatial_node_index;

                    let Some(clipped_surface_rect) = surface.get_surface_rect(
                        &coverage_rect,
                        frame_context.spatial_tree,
                    ) else {
                        return;
                    };

                    // Draw a normal screens-space mask to an alpha target that
                    // can be sampled when compositing this picture.
                    let empty_task = EmptyTask {
                        content_origin: clipped_surface_rect.min.to_f32(),
                        device_pixel_scale,
                        raster_spatial_node_index,
                    };

                    let task_size = clipped_surface_rect.size();

                    let clip_task_id = frame_state.rg_builder.add().init(RenderTask::new_dynamic(
                        task_size,
                        RenderTaskKind::Empty(empty_task),
                    ));

                    // Construct a new clip node range, also add image-mask dependencies as needed
                    let first_clip_node_index = frame_state.clip_store.clip_node_instances.len() as u32;
                    for instance in target_masks {
                        let clip_instance = frame_state.clip_store.get_instance_from_range(&prim_instance.vis.clip_chain.clips_range, instance);

                        for tile in frame_state.clip_store.visible_mask_tiles(clip_instance) {
                            frame_state.rg_builder.add_dependency(
                                clip_task_id,
                                tile.task_id,
                            );
                        }

                        frame_state.clip_store.clip_node_instances.push(clip_instance.clone());
                    }

                    let clip_node_range = ClipNodeRange {
                        first: first_clip_node_index,
                        count: frame_state.clip_store.clip_node_instances.len() as u32 - first_clip_node_index,
                    };

                    let masks = MaskSubPass {
                        clip_node_range,
                        prim_spatial_node_index,
                        prim_address_f,
                    };

                    let clip_task = frame_state.rg_builder.get_task_mut(clip_task_id);
                    clip_task.add_sub_pass(SubPass::Masks {
                        masks,
                    });

                    let clip_task_index = ClipTaskIndex(scratch.clip_mask_instances.len() as _);
                    scratch.clip_mask_instances.push(ClipMaskKind::Mask(clip_task_id));
                    prim_instance.vis.clip_task_index = clip_task_index;
                    frame_state.surface_builder.add_child_render_task(
                        clip_task_id,
                        frame_state.rg_builder,
                    );
                }
            }

            if pic.prepare_for_render(
                frame_state,
                data_stores,
            ) {
                if let Picture3DContext::In { root_data: None, plane_splitter_index, .. } = pic.context_3d {
                    let dirty_rect = frame_state.current_dirty_region().combined;
                    let visibility_node = frame_state.current_dirty_region().visibility_spatial_node;
                    let splitter = &mut frame_state.plane_splitters[plane_splitter_index.0];
                    let surface_index = pic.raster_config.as_ref().unwrap().surface_index;
                    let surface = &frame_state.surfaces[surface_index.0];
                    let local_prim_rect = surface.clipped_local_rect.cast_unit();

                    PicturePrimitive::add_split_plane(
                        splitter,
                        frame_context.spatial_tree,
                        prim_spatial_node_index,
                        visibility_node,
                        local_prim_rect,
                        &prim_instance.vis.clip_chain.local_clip_rect,
                        dirty_rect,
                        plane_split_anchor,
                    );
                }
            } else {
                prim_instance.clear_visibility();
            }
        }
        PrimitiveInstanceKind::BackdropCapture { .. } => {
            // Register the owner picture of this backdrop primitive as the
            // target for resolve of the sub-graph
            frame_state.surface_builder.register_resolve_source();

            if frame_context.debug_flags.contains(DebugFlags::HIGHLIGHT_BACKDROP_FILTERS) {
                if let Some(world_rect) = pic_state.map_pic_to_vis.map(&prim_instance.vis.clip_chain.pic_coverage_rect) {
                    scratch.push_debug_rect(
                        world_rect.cast_unit(),
                        2,
                        crate::debug_colors::MAGENTA,
                        ColorF::TRANSPARENT,
                    );
                }
            }
        }
        PrimitiveInstanceKind::BackdropRender { pic_index, .. } => {
            match frame_state.surface_builder.sub_graph_output_map.get(pic_index).cloned() {
                Some(sub_graph_output_id) => {
                    frame_state.surface_builder.add_child_render_task(
                        sub_graph_output_id,
                        frame_state.rg_builder,
                    );
                }
                None => {
                    // Backdrop capture was found not visible, didn't produce a sub-graph
                    // so we can just skip drawing
                    prim_instance.clear_visibility();
                }
            }
        }
    }

    match prim_instance.vis.state {
        VisibilityState::Unset => {
            panic!("bug: invalid vis state");
        }
        VisibilityState::Visible { .. } => {
            frame_state.push_prim(
                &PrimitiveCommand::simple(prim_instance_index),
                prim_spatial_node_index,
                targets,
            );
        }
        VisibilityState::PassThrough | VisibilityState::Culled => {}
    }
}


fn write_segment<F>(
    segment_instance_index: SegmentInstanceIndex,
    frame_state: &mut FrameBuildingState,
    segments: &mut SegmentStorage,
    segment_instances: &mut SegmentInstanceStorage,
    f: F,
) where F: Fn(&mut GpuDataRequest) {
    debug_assert_ne!(segment_instance_index, SegmentInstanceIndex::INVALID);
    if segment_instance_index != SegmentInstanceIndex::UNUSED {
        let segment_instance = &mut segment_instances[segment_instance_index];

        if let Some(mut request) = frame_state.gpu_cache.request(&mut segment_instance.gpu_cache_handle) {
            let segments = &segments[segment_instance.segments_range];

            f(&mut request);

            for segment in segments {
                request.write_segment(
                    segment.local_rect,
                    [0.0; 4],
                );
            }
        }
    }
}

fn decompose_repeated_gradient(
    prim_vis: &PrimitiveVisibility,
    prim_local_rect: &LayoutRect,
    prim_spatial_node_index: SpatialNodeIndex,
    stretch_size: &LayoutSize,
    tile_spacing: &LayoutSize,
    frame_state: &mut FrameBuildingState,
    gradient_tiles: &mut GradientTileStorage,
    spatial_tree: &SpatialTree,
    mut callback: Option<&mut dyn FnMut(&LayoutRect, GpuDataRequest)>,
) -> GradientTileRange {
    let tile_range = gradient_tiles.open_range();

    // Tighten the clip rect because decomposing the repeated image can
    // produce primitives that are partially covering the original image
    // rect and we want to clip these extra parts out.
    if let Some(tight_clip_rect) = prim_vis
        .clip_chain
        .local_clip_rect
        .intersection(prim_local_rect) {

        let visible_rect = compute_conservative_visible_rect(
            &prim_vis.clip_chain,
            frame_state.current_dirty_region().combined,
            frame_state.current_dirty_region().visibility_spatial_node,
            prim_spatial_node_index,
            spatial_tree,
        );
        let stride = *stretch_size + *tile_spacing;

        let repetitions = image_tiling::repetitions(prim_local_rect, &visible_rect, stride);
        gradient_tiles.reserve(repetitions.num_repetitions());
        for Repetition { origin, .. } in repetitions {
            let mut handle = GpuCacheHandle::new();
            let rect = LayoutRect::from_origin_and_size(
                origin,
                *stretch_size,
            );

            if let Some(callback) = &mut callback {
                if let Some(request) = frame_state.gpu_cache.request(&mut handle) {
                    callback(&rect, request);
                }
            }

            gradient_tiles.push(VisibleGradientTile {
                local_rect: rect,
                local_clip_rect: tight_clip_rect,
                handle
            });
        }
    }

    // At this point if we don't have tiles to show it means we could probably
    // have done a better a job at culling during an earlier stage.
    gradient_tiles.close_range(tile_range)
}


fn update_clip_task_for_brush(
    instance: &PrimitiveInstance,
    prim_origin: &LayoutPoint,
    prim_spatial_node_index: SpatialNodeIndex,
    root_spatial_node_index: SpatialNodeIndex,
    visibility_spatial_node_index: SpatialNodeIndex,
    pic_context: &PictureContext,
    pic_state: &mut PictureState,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    prim_store: &PrimitiveStore,
    data_stores: &mut DataStores,
    segments_store: &mut SegmentStorage,
    segment_instances_store: &mut SegmentInstanceStorage,
    clip_mask_instances: &mut Vec<ClipMaskKind>,
    device_pixel_scale: DevicePixelScale,
) -> Option<ClipTaskIndex> {
    let segments = match instance.kind {
        PrimitiveInstanceKind::BoxShadow { .. } => {
            unreachable!("BUG: box-shadows should not hit legacy brush clip path");
        }
        PrimitiveInstanceKind::Picture { .. } |
        PrimitiveInstanceKind::TextRun { .. } |
        PrimitiveInstanceKind::Clear { .. } |
        PrimitiveInstanceKind::LineDecoration { .. } |
        PrimitiveInstanceKind::BackdropCapture { .. } |
        PrimitiveInstanceKind::BackdropRender { .. } => {
            return None;
        }
        PrimitiveInstanceKind::Image { image_instance_index, .. } => {
            let segment_instance_index = prim_store
                .images[image_instance_index]
                .segment_instance_index;

            if segment_instance_index == SegmentInstanceIndex::UNUSED {
                return None;
            }

            let segment_instance = &segment_instances_store[segment_instance_index];

            &segments_store[segment_instance.segments_range]
        }
        PrimitiveInstanceKind::YuvImage { segment_instance_index, .. } => {
            debug_assert!(segment_instance_index != SegmentInstanceIndex::INVALID);

            if segment_instance_index == SegmentInstanceIndex::UNUSED {
                return None;
            }

            let segment_instance = &segment_instances_store[segment_instance_index];

            &segments_store[segment_instance.segments_range]
        }
        PrimitiveInstanceKind::Rectangle { use_legacy_path, segment_instance_index, .. } => {
            assert!(use_legacy_path);
            debug_assert!(segment_instance_index != SegmentInstanceIndex::INVALID);

            if segment_instance_index == SegmentInstanceIndex::UNUSED {
                return None;
            }

            let segment_instance = &segment_instances_store[segment_instance_index];

            &segments_store[segment_instance.segments_range]
        }
        PrimitiveInstanceKind::ImageBorder { data_handle, .. } => {
            let border_data = &data_stores.image_border[data_handle].kind;

            // TODO: This is quite messy - once we remove legacy primitives we
            //       can change this to be a tuple match on (instance, template)
            border_data.brush_segments.as_slice()
        }
        PrimitiveInstanceKind::NormalBorder { data_handle, .. } => {
            let border_data = &data_stores.normal_border[data_handle].kind;

            // TODO: This is quite messy - once we remove legacy primitives we
            //       can change this to be a tuple match on (instance, template)
            border_data.brush_segments.as_slice()
        }
        PrimitiveInstanceKind::LinearGradient { data_handle, .. }
        | PrimitiveInstanceKind::CachedLinearGradient { data_handle, .. } => {
            let prim_data = &data_stores.linear_grad[data_handle];

            // TODO: This is quite messy - once we remove legacy primitives we
            //       can change this to be a tuple match on (instance, template)
            if prim_data.brush_segments.is_empty() {
                return None;
            }

            prim_data.brush_segments.as_slice()
        }
        PrimitiveInstanceKind::RadialGradient { data_handle, .. } => {
            let prim_data = &data_stores.radial_grad[data_handle];

            // TODO: This is quite messy - once we remove legacy primitives we
            //       can change this to be a tuple match on (instance, template)
            if prim_data.brush_segments.is_empty() {
                return None;
            }

            prim_data.brush_segments.as_slice()
        }
        PrimitiveInstanceKind::ConicGradient { data_handle, .. } => {
            let prim_data = &data_stores.conic_grad[data_handle];

            // TODO: This is quite messy - once we remove legacy primitives we
            //       can change this to be a tuple match on (instance, template)
            if prim_data.brush_segments.is_empty() {
                return None;
            }

            prim_data.brush_segments.as_slice()
        }
    };

    // If there are no segments, early out to avoid setting a valid
    // clip task instance location below.
    if segments.is_empty() {
        return None;
    }

    // Set where in the clip mask instances array the clip mask info
    // can be found for this primitive. Each segment will push the
    // clip mask information for itself in update_clip_task below.
    let clip_task_index = ClipTaskIndex(clip_mask_instances.len() as _);

    // If we only built 1 segment, there is no point in re-running
    // the clip chain builder. Instead, just use the clip chain
    // instance that was built for the main primitive. This is a
    // significant optimization for the common case.
    if segments.len() == 1 {
        let clip_mask_kind = update_brush_segment_clip_task(
            &segments[0],
            Some(&instance.vis.clip_chain),
            root_spatial_node_index,
            pic_context.surface_index,
            frame_context,
            frame_state,
            &mut data_stores.clip,
            device_pixel_scale,
        );
        clip_mask_instances.push(clip_mask_kind);
    } else {
        let dirty_rect = frame_state.current_dirty_region().combined;

        for segment in segments {
            // Build a clip chain for the smaller segment rect. This will
            // often manage to eliminate most/all clips, and sometimes
            // clip the segment completely.
            frame_state.clip_store.set_active_clips_from_clip_chain(
                &instance.vis.clip_chain,
                prim_spatial_node_index,
                visibility_spatial_node_index,
                &frame_context.spatial_tree,
                &data_stores.clip,
            );

            let segment_clip_chain = frame_state
                .clip_store
                .build_clip_chain_instance(
                    segment.local_rect.translate(prim_origin.to_vector()),
                    &pic_state.map_local_to_pic,
                    &pic_state.map_pic_to_vis,
                    &frame_context.spatial_tree,
                    frame_state.gpu_cache,
                    frame_state.resource_cache,
                    device_pixel_scale,
                    &dirty_rect,
                    &mut data_stores.clip,
                    frame_state.rg_builder,
                    false,
                );

            let clip_mask_kind = update_brush_segment_clip_task(
                &segment,
                segment_clip_chain.as_ref(),
                root_spatial_node_index,
                pic_context.surface_index,
                frame_context,
                frame_state,
                &mut data_stores.clip,
                device_pixel_scale,
            );
            clip_mask_instances.push(clip_mask_kind);
        }
    }

    Some(clip_task_index)
}

pub fn update_clip_task(
    instance: &mut PrimitiveInstance,
    prim_origin: &LayoutPoint,
    prim_spatial_node_index: SpatialNodeIndex,
    root_spatial_node_index: SpatialNodeIndex,
    visibility_spatial_node_index: SpatialNodeIndex,
    pic_context: &PictureContext,
    pic_state: &mut PictureState,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    prim_store: &mut PrimitiveStore,
    data_stores: &mut DataStores,
    scratch: &mut PrimitiveScratchBuffer,
) -> bool {
    let device_pixel_scale = frame_state.surfaces[pic_context.surface_index.0].device_pixel_scale;

    build_segments_if_needed(
        instance,
        frame_state,
        prim_store,
        data_stores,
        &mut scratch.segments,
        &mut scratch.segment_instances,
    );

    // First try to  render this primitive's mask using optimized brush rendering.
    instance.vis.clip_task_index = if let Some(clip_task_index) = update_clip_task_for_brush(
        instance,
        prim_origin,
        prim_spatial_node_index,
        root_spatial_node_index,
        visibility_spatial_node_index,
        pic_context,
        pic_state,
        frame_context,
        frame_state,
        prim_store,
        data_stores,
        &mut scratch.segments,
        &mut scratch.segment_instances,
        &mut scratch.clip_mask_instances,
        device_pixel_scale,
    ) {
        clip_task_index
    } else if instance.vis.clip_chain.needs_mask {
        // Get a minimal device space rect, clipped to the screen that we
        // need to allocate for the clip mask, as well as interpolated
        // snap offsets.
        let unadjusted_device_rect = match frame_state.surfaces[pic_context.surface_index.0].get_surface_rect(
            &instance.vis.clip_chain.pic_coverage_rect,
            frame_context.spatial_tree,
        ) {
            Some(rect) => rect,
            None => return false,
        };

        let (device_rect, device_pixel_scale) = adjust_mask_scale_for_max_size(
            unadjusted_device_rect,
            device_pixel_scale,
        );

        if device_rect.size().to_i32().is_empty() {
            log::warn!("Bad adjusted clip task size {:?} (was {:?})", device_rect.size(), unadjusted_device_rect.size());
            return false;
        }

        let clip_task_id = RenderTaskKind::new_mask(
            device_rect,
            instance.vis.clip_chain.clips_range,
            root_spatial_node_index,
            frame_state.clip_store,
            frame_state.gpu_cache,
            &mut frame_state.frame_gpu_data.f32,
            frame_state.resource_cache,
            frame_state.rg_builder,
            &mut data_stores.clip,
            device_pixel_scale,
            frame_context.fb_config,
            &mut frame_state.surface_builder,
        );
        // Set the global clip mask instance for this primitive.
        let clip_task_index = ClipTaskIndex(scratch.clip_mask_instances.len() as _);
        scratch.clip_mask_instances.push(ClipMaskKind::Mask(clip_task_id));
        instance.vis.clip_task_index = clip_task_index;
        frame_state.surface_builder.add_child_render_task(
            clip_task_id,
            frame_state.rg_builder,
        );
        clip_task_index
    } else {
        ClipTaskIndex::INVALID
    };

    true
}

/// Write out to the clip mask instances array the correct clip mask
/// config for this segment.
pub fn update_brush_segment_clip_task(
    segment: &BrushSegment,
    clip_chain: Option<&ClipChainInstance>,
    root_spatial_node_index: SpatialNodeIndex,
    surface_index: SurfaceIndex,
    frame_context: &FrameBuildingContext,
    frame_state: &mut FrameBuildingState,
    clip_data_store: &mut ClipDataStore,
    device_pixel_scale: DevicePixelScale,
) -> ClipMaskKind {
    let clip_chain = match clip_chain {
        Some(chain) => chain,
        None => return ClipMaskKind::Clipped,
    };
    if !clip_chain.needs_mask ||
       (!segment.may_need_clip_mask && !clip_chain.has_non_local_clips) {
        return ClipMaskKind::None;
    }

    let unadjusted_device_rect = match frame_state.surfaces[surface_index.0].get_surface_rect(
        &clip_chain.pic_coverage_rect,
        frame_context.spatial_tree,
    ) {
        Some(rect) => rect,
        None => return ClipMaskKind::Clipped,
    };

    let (device_rect, device_pixel_scale) = adjust_mask_scale_for_max_size(unadjusted_device_rect, device_pixel_scale);

    if device_rect.size().to_i32().is_empty() {
        log::warn!("Bad adjusted mask size {:?} (was {:?})", device_rect.size(), unadjusted_device_rect.size());
        return ClipMaskKind::Clipped;
    }

    let clip_task_id = RenderTaskKind::new_mask(
        device_rect,
        clip_chain.clips_range,
        root_spatial_node_index,
        frame_state.clip_store,
        frame_state.gpu_cache,
        &mut frame_state.frame_gpu_data.f32,
        frame_state.resource_cache,
        frame_state.rg_builder,
        clip_data_store,
        device_pixel_scale,
        frame_context.fb_config,
        &mut frame_state.surface_builder,
    );

    frame_state.surface_builder.add_child_render_task(
        clip_task_id,
        frame_state.rg_builder,
    );
    ClipMaskKind::Mask(clip_task_id)
}


fn write_brush_segment_description(
    prim_local_rect: LayoutRect,
    prim_local_clip_rect: LayoutRect,
    clip_chain: &ClipChainInstance,
    segment_builder: &mut SegmentBuilder,
    clip_store: &ClipStore,
    data_stores: &DataStores,
) -> bool {
    // If the brush is small, we want to skip building segments
    // and just draw it as a single primitive with clip mask.
    if prim_local_rect.area() < MIN_BRUSH_SPLIT_AREA {
        return false;
    }

    // NOTE: The local clip rect passed to the segment builder must be the unmodified
    //       local clip rect from the clip leaf, not the local_clip_rect from the
    //       clip-chain instance. The clip-chain instance may have been reduced by
    //       clips that are in the same coordinate system, but not the same spatial
    //       node as the primitive. This can result in the clip for the segment building
    //       being affected by scrolling clips, which we can't handle (since the segments
    //       are not invalidated during frame building after being built).
    segment_builder.initialize(
        prim_local_rect,
        None,
        prim_local_clip_rect,
    );

    // Segment the primitive on all the local-space clip sources that we can.
    for i in 0 .. clip_chain.clips_range.count {
        let clip_instance = clip_store
            .get_instance_from_range(&clip_chain.clips_range, i);
        let clip_node = &data_stores.clip[clip_instance.handle];

        // If this clip item is positioned by another positioning node, its relative position
        // could change during scrolling. This means that we would need to resegment. Instead
        // of doing that, only segment with clips that have the same positioning node.
        // TODO(mrobinson, #2858): It may make sense to include these nodes, resegmenting only
        // when necessary while scrolling.
        if !clip_instance.flags.contains(ClipNodeFlags::SAME_SPATIAL_NODE) {
            continue;
        }

        let (local_clip_rect, radius, mode) = match clip_node.item.kind {
            ClipItemKind::RoundedRectangle { rect, radius, mode } => {
                (rect, Some(radius), mode)
            }
            ClipItemKind::Rectangle { rect, mode } => {
                (rect, None, mode)
            }
            ClipItemKind::BoxShadow { ref source } => {
                // For inset box shadows, we can clip out any
                // pixels that are inside the shadow region
                // and are beyond the inner rect, as they can't
                // be affected by the blur radius.
                let inner_clip_mode = match source.clip_mode {
                    BoxShadowClipMode::Outset => None,
                    BoxShadowClipMode::Inset => Some(ClipMode::ClipOut),
                };

                // Push a region into the segment builder where the
                // box-shadow can have an effect on the result. This
                // ensures clip-mask tasks get allocated for these
                // pixel regions, even if no other clips affect them.
                segment_builder.push_mask_region(
                    source.prim_shadow_rect,
                    source.prim_shadow_rect.inflate(
                        -0.5 * source.original_alloc_size.width,
                        -0.5 * source.original_alloc_size.height,
                    ),
                    inner_clip_mode,
                );

                continue;
            }
            ClipItemKind::Image { .. } => {
                panic!("bug: masks not supported on old segment path");
            }
        };

        segment_builder.push_clip_rect(local_clip_rect, radius, mode);
    }

    true
}

fn build_segments_if_needed(
    instance: &mut PrimitiveInstance,
    frame_state: &mut FrameBuildingState,
    prim_store: &mut PrimitiveStore,
    data_stores: &DataStores,
    segments_store: &mut SegmentStorage,
    segment_instances_store: &mut SegmentInstanceStorage,
) {
    let prim_clip_chain = &instance.vis.clip_chain;

    // Usually, the primitive rect can be found from information
    // in the instance and primitive template.
    let prim_local_rect = data_stores.get_local_prim_rect(
        instance,
        &prim_store.pictures,
        frame_state.surfaces,
    );

    let segment_instance_index = match instance.kind {
        PrimitiveInstanceKind::Rectangle { use_legacy_path, ref mut segment_instance_index, .. } => {
            assert!(use_legacy_path);
            segment_instance_index
        }
        PrimitiveInstanceKind::YuvImage { ref mut segment_instance_index, compositor_surface_kind, .. } => {
            // Only use segments for YUV images if not drawing as a compositor surface
            if !compositor_surface_kind.supports_segments() {
                *segment_instance_index = SegmentInstanceIndex::UNUSED;
                return;
            }

            segment_instance_index
        }
        PrimitiveInstanceKind::Image { data_handle, image_instance_index, compositor_surface_kind, .. } => {
            let image_data = &data_stores.image[data_handle].kind;
            let image_instance = &mut prim_store.images[image_instance_index];

            //Note: tiled images don't support automatic segmentation,
            // they strictly produce one segment per visible tile instead.
            if !compositor_surface_kind.supports_segments() ||
                frame_state.resource_cache
                    .get_image_properties(image_data.key)
                    .and_then(|properties| properties.tiling)
                    .is_some()
            {
                image_instance.segment_instance_index = SegmentInstanceIndex::UNUSED;
                return;
            }
            &mut image_instance.segment_instance_index
        }
        PrimitiveInstanceKind::Picture { .. } |
        PrimitiveInstanceKind::TextRun { .. } |
        PrimitiveInstanceKind::NormalBorder { .. } |
        PrimitiveInstanceKind::ImageBorder { .. } |
        PrimitiveInstanceKind::Clear { .. } |
        PrimitiveInstanceKind::LinearGradient { .. } |
        PrimitiveInstanceKind::CachedLinearGradient { .. } |
        PrimitiveInstanceKind::RadialGradient { .. } |
        PrimitiveInstanceKind::ConicGradient { .. } |
        PrimitiveInstanceKind::LineDecoration { .. } |
        PrimitiveInstanceKind::BackdropCapture { .. } |
        PrimitiveInstanceKind::BackdropRender { .. } => {
            // These primitives don't support / need segments.
            return;
        }
        PrimitiveInstanceKind::BoxShadow { .. } => {
            unreachable!("BUG: box-shadows should not hit legacy brush clip path");
        }
    };

    if *segment_instance_index == SegmentInstanceIndex::INVALID {
        let mut segments: SmallVec<[BrushSegment; 8]> = SmallVec::new();
        let clip_leaf = frame_state.clip_tree.get_leaf(instance.clip_leaf_id);

        if write_brush_segment_description(
            prim_local_rect,
            clip_leaf.local_clip_rect,
            prim_clip_chain,
            &mut frame_state.segment_builder,
            frame_state.clip_store,
            data_stores,
        ) {
            frame_state.segment_builder.build(|segment| {
                segments.push(
                    BrushSegment::new(
                        segment.rect.translate(-prim_local_rect.min.to_vector()),
                        segment.has_mask,
                        segment.edge_flags,
                        [0.0; 4],
                        BrushFlags::PERSPECTIVE_INTERPOLATION,
                    ),
                );
            });
        }

        // If only a single segment is produced, there is no benefit to writing
        // a segment instance array. Instead, just use the main primitive rect
        // written into the GPU cache.
        // TODO(gw): This is (sortof) a bandaid - due to a limitation in the current
        //           brush encoding, we can only support a total of up to 2^16 segments.
        //           This should be (more than) enough for any real world case, so for
        //           now we can handle this by skipping cases where we were generating
        //           segments where there is no benefit. The long term / robust fix
        //           for this is to move the segment building to be done as a more
        //           limited nine-patch system during scene building, removing arbitrary
        //           segmentation during frame-building (see bug #1617491).
        if segments.len() <= 1 {
            *segment_instance_index = SegmentInstanceIndex::UNUSED;
        } else {
            let segments_range = segments_store.extend(segments);

            let instance = SegmentedInstance {
                segments_range,
                gpu_cache_handle: GpuCacheHandle::new(),
            };

            *segment_instance_index = segment_instances_store.push(instance);
        };
    }
}

// Ensures that the size of mask render tasks are within MAX_MASK_SIZE.
fn adjust_mask_scale_for_max_size(device_rect: DeviceIntRect, device_pixel_scale: DevicePixelScale) -> (DeviceIntRect, DevicePixelScale) {
    if device_rect.width() > MAX_MASK_SIZE || device_rect.height() > MAX_MASK_SIZE {
        // round_out will grow by 1 integer pixel if origin is on a
        // fractional position, so keep that margin for error with -1:
        let device_rect_f = device_rect.to_f32();
        let scale = (MAX_MASK_SIZE - 1) as f32 /
            f32::max(device_rect_f.width(), device_rect_f.height());
        let new_device_pixel_scale = device_pixel_scale * Scale::new(scale);
        let new_device_rect = (device_rect_f * Scale::new(scale))
            .round_out()
            .to_i32();
        (new_device_rect, new_device_pixel_scale)
    } else {
        (device_rect, device_pixel_scale)
    }
}

impl CompositorSurfaceKind {
    /// Returns true if the compositor surface strategy supports segment rendering
    fn supports_segments(&self) -> bool {
        match self {
            CompositorSurfaceKind::Underlay | CompositorSurfaceKind::Overlay => false,
            CompositorSurfaceKind::Blit => true,
        }
    }
}

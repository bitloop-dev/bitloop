#include <bitloop/core/camera.h>
#include <bitloop/core/scene.h>
#include <bitloop/core/viewport.h>
#include <bitloop/imguix/imguix.h>
#include <bitloop/platform/platform.h>
//#include <bitloop/util/constexpr_dispatch_mappings.h>

BL_BEGIN_NS

DVec2 CameraInfo::originWorldOffset(const SurfaceInfo& surface) const
{
    return originPixelOffset(surface) / (zoom_64 * surface.initialSizeScale());
}

bool CameraInfo::operator ==(const CameraInfo& rhs) const
{
    if (max_precision != rhs.max_precision) return false;

    if (max_precision == FloatType::FQD) {
        if (pos_256 != rhs.pos_256) return false;
    }
    else if (max_precision == FloatType::FDD) {
        if (pos_128 != rhs.pos_128) return false;
    }
    else {
        if (pos_64 != rhs.pos_64) return false;
    }

    if (stretch_64 != rhs.stretch_64) return false;
    if (zoom_64 != rhs.zoom_64) return false;
    if (rotation_64 != rhs.rotation_64) return false;
    if (cam_pan != rhs.cam_pan) return false;
    if (ref_zoom != rhs.ref_zoom) return false;
    if (viewport_anchor != rhs.viewport_anchor) return false;
    if (init_pos != rhs.init_pos) return false;
    if (init_zoom != rhs.init_zoom) return false;
    if (init_stretch != rhs.init_stretch) return false;
    if (init_rotation != rhs.init_rotation) return false;
    return true;
}

CameraInfo& CameraInfo::operator =(const CameraInfo& rhs)
{
    if (*this != rhs)
    {
        max_precision = rhs.max_precision;

        zoom_64 = rhs.zoom_64;
        stretch_64 = rhs.stretch_64;
        rotation_64 = rhs.rotation_64;
        cam_pan = rhs.cam_pan;
        ref_zoom = rhs.ref_zoom;
        viewport_anchor = rhs.viewport_anchor;
        init_pos = rhs.init_pos;
        init_zoom = rhs.init_zoom;
        init_stretch = rhs.init_stretch;
        init_rotation = rhs.init_rotation;

        if (max_precision == FloatType::FQD)
        {
            pos_256 = rhs.pos_256;
            posChanged<fqd>();
        }
        else if (max_precision == FloatType::FDD)
        {
            pos_128 = rhs.pos_128;
            posChanged<fdd>();
        }
        else
        {
            pos_64 = rhs.pos_64;
            posChanged<f64>();
        }

        zoomChanged();
    }
    return *this;
}

WorldStageTransform CameraInfo::transformFor(const SurfaceInfo& surface) const
{
    WorldStageTransform transform;
    transform.setMaxPrecision(max_precision);
    transform.reset();

    const DVec2 adjusted_zoom = zoom_xy * surface.initialSizeScale();
    const DVec2 origin_offset = originPixelOffset(surface);

    if (transform.maxPrecision() >= FloatType::FQD)
    {
        transform.translate(
            fqd{ origin_offset.x } + fqd{ pan_x },
            fqd{ origin_offset.y } + fqd{ pan_y }
        );
        transform.rotate(rotation_64);
        transform.translate(
            -x_256 * fqd{ adjusted_zoom.x },
            -y_256 * fqd{ adjusted_zoom.y }
        );
        transform.scale(
            fqd{ adjusted_zoom.x },
            fqd{ adjusted_zoom.y }
        );
    }
    else if (transform.maxPrecision() >= FloatType::FDD)
    {
        transform.translate(
            fdd{ origin_offset.x } + fdd{ pan_x },
            fdd{ origin_offset.y } + fdd{ pan_y }
        );
        transform.rotate(rotation_64);
        transform.translate(
            -x_128 * fdd{ adjusted_zoom.x },
            -y_128 * fdd{ adjusted_zoom.y }
        );
        transform.scale(
            fdd{ adjusted_zoom.x },
            fdd{ adjusted_zoom.y }
        );
    }
    else
    {
        transform.translate(origin_offset.x + pan_x, origin_offset.y + pan_y);
        transform.rotate(rotation_64);
        transform.translate(-x_64 * adjusted_zoom.x, -y_64 * adjusted_zoom.y);
        transform.scale(adjusted_zoom.x, adjusted_zoom.y);
    }

    return transform;
}

void CameraInfo::setOriginViewportAnchor(double ax, double ay)
{
    viewport_anchor = { ax, ay };
}

DVec2 bl::CameraInfo::viewportStageSize(const SurfaceInfo& surface) const
{
    return surface.size();
}

void CameraInfo::setOriginViewportAnchor(Anchor anchor)
{
    switch (anchor)
    {
        case Anchor::TOP_LEFT:     setOriginViewportAnchor(0.0, 0.0);  break;
        case Anchor::TOP:          setOriginViewportAnchor(0.5, 0.0);  break;
        case Anchor::TOP_RIGHT:    setOriginViewportAnchor(1.0, 0.0);  break;
        case Anchor::LEFT:         setOriginViewportAnchor(0.0, 0.5);  break;
        case Anchor::CENTER:       setOriginViewportAnchor(0.5, 0.5);  break;
        case Anchor::RIGHT:        setOriginViewportAnchor(1.0, 0.5);  break;
        case Anchor::BOTTOM_LEFT:  setOriginViewportAnchor(0.0, 1.0);  break;
        case Anchor::BOTTOM:       setOriginViewportAnchor(0.5, 1.0);  break;
        case Anchor::BOTTOM_RIGHT: setOriginViewportAnchor(1.0, 1.0);  break;
    }
}

DVec2 CameraInfo::originPixelOffset(const SurfaceInfo& surface) const
{
    const double viewport_cx = surface.width() / 2.0;
    const double viewport_cy = surface.height() / 2.0;

    return Vec2(
        viewport_cx + surface.width() * (viewport_anchor.x - 0.5),
        viewport_cy + surface.height() * (viewport_anchor.y - 0.5)
    );
}

// Scale world to fit viewport rect
void CameraInfo::cameraToViewport(
    const SurfaceInfo& surface,
    f64 left,
    f64 top,
    f64 right,
    f64 bottom)
{
    double world_w = (right - left);
    double world_h = (bottom - top);

    rotation_64 = 0;
    zoom_x = surface.width() / world_w;
    zoom_y = surface.height() / world_h;
    x_256  = (surface.width() / 2.0) / zoom_x;
    y_256  = (surface.height() / 2.0) / zoom_y;

    posChanged<fqd>();
    zoomChanged();
}

void CameraInfo::focusWorldRect(
    const SurfaceInfo& surface,
    fqd x0, fqd y0, fqd x1, fqd y1,
    bool stretch)
{
    fqd world_w = x1 - x0;
    fqd world_h = y1 - y0;
    rotation_64 = 0;
    pan_x = 0;
    pan_y = 0;

    if (stretch)
    {
        f64 zx = (f64)((fqd)surface.width() / world_w);
        f64 zy = (f64)((fqd)surface.height() / world_h);
        zoom_64 = DVec2{ zx, zy }.mag();
        sx_64 = (zx / zoom_64);
        sy_64 = (zy / zoom_64);

        const DVec2 origin_world_offset = originWorldOffset(surface);
        x_256 = x0 + origin_world_offset.x;
        y_256 = y0 + origin_world_offset.y;
    }
    else
    {
        fqd aspect_view = fqd(surface.width() / surface.height());
        fqd aspect_rect = world_w / world_h;

        // Not re-stretching, but preserve existing stretch aspect ratio?
        stretch_64 = { 1, 1 };

        if (aspect_rect > aspect_view)
        {
            // Shrink Height, gap top and bottom
            zoom_64 = (f64)((fqd)surface.width() / world_w);

            assert(zoom_64 > 0.0);

            const DVec2 origin_world_offset = originWorldOffset(surface);

            fqd world_ox{ 0.0 };
            fqd world_oy = (((fqd)surface.height() - (world_h * (fqd)zoom_64)) / 2.0) / (fqd)zoom_64;

            x_256 = origin_world_offset.x + x0 - world_ox;
            y_256 = origin_world_offset.y + y0 - world_oy;
        }
        else
        {
            // Shrink Width, gap left and right
            zoom_64 = (f64)((fqd)surface.height() / world_h);

            assert(zoom_64 > 0.0);

            const DVec2 origin_world_offset = originWorldOffset(surface);

            fqd world_ox = (((fqd)surface.width() - (world_w * (fqd)zoom_64)) / 2.0) / (fqd)zoom_64;
            fqd world_oy{ 0.0 };

            x_256 = origin_world_offset.x + x0 - world_ox;
            y_256 = origin_world_offset.y + y0 - world_oy;
        }
    }

    ref_zoom = zoom_64;

    posChanged<fqd>();
    zoomChanged();
}

void CameraInfo::originToCenterViewport()
{
    setPos(0, 0);
}

void CameraInfo::populateUI(DRect world_bounds)
{
    bool changed = false;

    float required_space = 0.0f;
    ImGui::IncreaseRequiredSpaceForLabel(required_space, "Zoom X/Y", scale_size(20.0f));

    int decimals = getPositionDecimals();
    char format[16];
    snprintf(format, sizeof(format), "%%.%df", decimals);


    ImGui::SetNextItemWidthForSpace(required_space);
    //if (ImGui::RevertableDragFloat128("X", &x_128, &init_pos.x, 1.0 / zoom_128, fdd{ restrict_world_rect.x1 }, fdd{ restrict_world_rect.x2 }, format))
    //    changed = true;
    //ImGui::SetNextItemWidthForSpace(required_space);
    //if (ImGui::RevertableDragFloat128("Y", &y_128, &init_pos.y, 1.0 / zoom_128, fdd{ restrict_world_rect.y1 }, fdd{ restrict_world_rect.y2 }, format))
    //    changed = true;

    bl_table_invoke(bl_dispatch_table_callable([&]<typename T>()
    {
        T cam_x = x<T>();
        if (ImGui::RevertableDragFltxCompact<T>("X", &cam_x, &init_pos.x, (T)(1.0 / zoom_64), (T)world_bounds.x1, (T)world_bounds.x2, decimals))
        {
            setX(cam_x);
            changed = true;
        }
    }), bl_enum_type(max_precision));
    ImGui::SetNextItemWidthForSpace(required_space);
    bl_table_invoke(bl_dispatch_table_callable([&]<typename T>()
    {
        T cam_y = y<T>();
        if (ImGui::RevertableDragFltxCompact<T>("Y", &cam_y, &init_pos.y, (T)(1.0 / zoom_64), (T)world_bounds.y1, (T)world_bounds.y2, decimals))
        {
            setY(cam_y);
            changed = true;
        }
    }), bl_enum_type(max_precision));

    ImGui::SetNextItemWidthForSpace(required_space);
    if (ImGui::RevertableSliderAngle("Rotation", &rotation_64, &init_rotation))
        changed = true;

    /// //fqd relative_zoom = relativeZoom();
    f64 relative_zoom = relativeZoom();
    /// //fqd zoom_speed = relative_zoom / 100.0;
    f64 zoom_speed = relative_zoom / 100.0;
    ImGui::SetNextItemWidthForSpace(required_space);
    /// //if (ImGui::RevertableDragFltx<fqd>("Zoom", &relative_zoom, &init_zoom, zoom_speed, 0.1_qd, 1.0e62_qd, 5))
    /// if (ImGui::RevertableDragFltx("Zoom", &relative_zoom, &init_zoom, zoom_speed, 0.1, 1.0e62, 5))
    /// {
    ///     setRelativeZoom((f64)relative_zoom);
    ///     changed = true;
    /// }
    if (ImGui::RevertableDragFltx("Zoom", &relative_zoom, &init_zoom, zoom_speed, 0.1, 1.0e62, 6))
    {
        zoom_speed = relative_zoom / 100.0;
        //zoom_64 = relative_zoom * getReferenceZoom();
        setRelativeZoom(relative_zoom);
        changed = true;
    }

    ImGui::SetNextItemWidthForSpace(required_space);
    if (ImGui::RevertableSliderDouble2("Zoom X/Y", stretch_64.data(), init_stretch.data(), 0.1, 10.0, "%.2fx"))
        changed = true;

    if (changed)
    {
        posChanged<fqd>();
        zoomChanged();
    }

    return;
}

void CameraNavigator::restrictRelativeZoomRange(f64 min, f64 max)
{
    min_zoom = min;
    max_zoom = max;

    if (!camera)
        return;
    if (camera->zoom() < camera->getReferenceZoom() * min_zoom)
        camera->setZoom(camera->getReferenceZoom() * min_zoom);
    if (camera->zoom() > camera->getReferenceZoom() * max_zoom)
        camera->setZoom(camera->getReferenceZoom() * max_zoom);
}

void CameraNavigator::panBegin(int _x, int _y, double touch_dist, double touch_angle)
{
    //blPrint("[DOWN] Cam Pos: (%.3f, %.3f) Zoom: (%.3f, %.3f) Angle: %.3f",
    //    (double)x_128, (double)y_128, (double)zoom_x, (double)zoom_y, cam_rotation);

    pan_down_touch_x = _x;
    pan_down_touch_y = _y;
    pan_down_touch_dist = touch_dist;
    pan_down_touch_angle = touch_angle;

    if (direct_cam_panning)
    {
        //DVec2 world_mouse = toWorld(x, y);
        pan_beg_cam_x = camera->x<fqd>();
        pan_beg_cam_y = camera->y<fqd>();
        pan_beg_cam_zoom = camera->zoom();
        ///pan_beg_cam_zoom_x = zoom_x;
        ///pan_beg_cam_zoom_y = zoom_y;
        pan_beg_cam_angle = camera->rotation();

        //last_pan_snapped_cam_grid_pos = DDVec2(x_128 * zoom_x, y_128 * zoom_y).snapped(fdd(cam_pos_stage_snap_size));
    }
    else
    {
        pan_beg_cam_x = camera->panX();
        pan_beg_cam_y = camera->panY();
    }
    panning = true;
}

bool CameraNavigator::panDrag(const SurfaceInfo& surface, int _x, int _y, double touch_dist, double touch_angle)
{
    //blPrint("[DRAG] Cam Pos: (%.3f, %.3f) Zoom: (%.3f, %.3f) Angle: %.3f",
    //    (double)x, (double)y, (double)zoom_x, (double)zoom_y, cam_rotation);

    bool changed = false;

    if (panning)
    {
        if (direct_cam_panning)
        {
            //DVec2 world_mouse = toWorld(_x, _y);
            //double dx = world_mouse.x - pan_down_touch_x;
            //double dy = world_mouse.y - pan_down_touch_y;
            int dx = _x - pan_down_touch_x;
            int dy = _y - pan_down_touch_y;


            QDVec2 world_offset = camera->transformFor(surface).toWorldOffset<fqd>(dx, dy);
            //blPrint("(dx,dy) = (%.3f, %.3f)", world_offset.x, world_offset.y);
            //blPrint() << "(dx,dy) = (" << bl::dp(3) << world_offset.x << ", " << world_offset.)y

            fqd new_cam_world_x = pan_beg_cam_x - world_offset.x;
            fqd new_cam_world_y = pan_beg_cam_y - world_offset.y;

            changed |= camera->setPos(
                new_cam_world_x,
                new_cam_world_y
            );

            /*DDVec2 potential_snapped_cam_grid_pos = DDVec2(new_cam_world_x * zoom_x, new_cam_world_y * zoom_y).snapped(cam_pos_stage_snap_size);

            if (potential_snapped_cam_grid_pos != last_pan_snapped_cam_grid_pos)
            {
                changed |= setPos(
                    potential_snapped_cam_grid_pos.x / zoom_x,
                    potential_snapped_cam_grid_pos.y / zoom_y
                );
            }
            
            if (changed)
            {
                last_pan_snapped_cam_grid_pos = potential_snapped_cam_grid_pos;
            }*/

            if (pan_down_touch_dist > 0.0)
            {
                double delta_zoom = touch_dist / pan_down_touch_dist;
                changed |= camera->setZoom(pan_beg_cam_zoom * delta_zoom);
            }

            if (fingers.size() >= 2)
            {
                double delta_rotation = math::closestAngleDifference(pan_down_touch_angle, touch_angle);
                changed |= camera->setRotation(pan_beg_cam_angle + delta_rotation);
                //blPrint() << "Setting rotation: " << bl::dp(3) << (double)cam_rotation;

                // todo: In order to lock camera during pan, you may need to manually
                //       send an event mimicking a mouse move in order to trigger pollEvents

                //project_worker()->queueEvent()
            }
        }
        else
        {
            int dx = _x - pan_down_touch_x;
            int dy = _y - pan_down_touch_y;

            changed |= camera->setPan(
                (f64)(pan_beg_cam_x + ((f64)dx/*/ zoom_x*/) /* * pan_mult*/),
                (f64)(pan_beg_cam_y + ((f64)dy/*/ zoom_y*/) /* * pan_mult*/)
            );
        }
    }

    return changed;
}

bool CameraNavigator::panZoomProcess(const SurfaceInfo& surface)
{
    bool changed = false;
    if (camera->zoom() < camera->getReferenceZoom() * min_zoom)
        changed |= camera->setZoom(camera->getReferenceZoom() * min_zoom);
    if (camera->zoom() > camera->getReferenceZoom() * max_zoom)
        changed |= camera->setZoom(camera->getReferenceZoom() * max_zoom);

    if (fingers.size() == 1)
    {
        changed |= panDrag(surface, (int)fingers[0].x, (int)fingers[0].y, 0.0, 0.0);
    }
    else if (fingers.size() == 2)
    {
        double avg_x = (fingers[0].x + fingers[1].x) / 2.0;
        double avg_y = (fingers[0].y + fingers[1].y) / 2.0;
        changed |= panDrag(surface, (int)avg_x, (int)avg_y, touchDist(), touchAngle());
    }

    return changed;
}

void CameraNavigator::panEnd()
{
    ///panDrag(_x, _y, 0, 0);
    panning = false;
}

bool CameraNavigator::handleWorldNavigation(Event event, bool single_touch_pan, bool zoom_anchor_mouse, double wheel_zoom_rate)
{
    if (!camera || !event.isPointerEvent())
        return false;

    Viewport* viewport = event.ctx_owner();
    if (!viewport)
        return false;

    PointerEvent e(event);

    // Support both single-finger pan & 2 finger transform
    switch (e.type())
    {
        case SDL_EVENT_FINGER_DOWN:
        {
            if (fingers.size() >= 2)
            {
                // Ignore 3 or more fingers
                return false;
            }

            // Add pressed finger
            FingerInfo info;
            info.fingerId = e.fingerID();
            info.x = e.x();
            info.y = e.y();
            fingers.push_back(info);

            if (fingers.size() == 1)
            {
                if (single_touch_pan)
                    panBegin((int)e.x(), (int)e.y(), 0.0, 0.0);
            }
            else if (fingers.size() == 2)
            {
                if (panning)
                {
                    // Was already panning with a single finger. Restart with 2 fingers
                    panEnd();
                }

                double avg_x = (fingers[0].x + fingers[1].x) / 2.0;
                double avg_y = (fingers[0].y + fingers[1].y) / 2.0;
                panBegin((int)avg_x, (int)avg_y, touchDist(), touchAngle());
            }

            //return true;
        }
        break;
        case SDL_EVENT_FINGER_UP:
        {
            if (fingers.size() > 2)
            {
                // Ignore 3 or more fingers
                return false;
            }

            double avg_x = 0.0;
            double avg_y = 0.0;
            if (fingers.size() == 2)
            {
                avg_x = (fingers[0].x + fingers[1].x) / 2.0;
                avg_y = (fingers[0].y + fingers[1].y) / 2.0;
            }

            // Erase lifted finger
            std::erase_if(fingers, [&](const FingerInfo& f) { return f.fingerId == e.fingerID(); });

            if (fingers.size() == 0)
            {
                // End single-finger pan (previously had 1 remaining pressed finger)
                if (single_touch_pan)
                    panEnd();
            }
            else if (fingers.size() == 1)
            {
                // End 2-finger pan, switch to single-finger pan
                panEnd();

                // Switch to whichever finger is still pressed (might not be finger index 0)
                if (single_touch_pan)
                    panBegin((int)fingers.front().x, (int)fingers.front().y, 0.0, 0.0);
            }
        }
        break;
        case SDL_EVENT_FINGER_MOTION:
        {
            if (fingers.size() > 2)
            {
                // Ignore 3 or more fingers
                return false;
            }

            // Update finger info
            for (size_t i = 0; i < fingers.size(); i++)
            {
                FingerInfo& info = fingers[i];
                if (info.fingerId == e.fingerID())
                {
                    info.x = e.x();
                    info.y = e.y();
                    break;
                }
            }

            return panZoomProcess(*viewport);
        }
        break;
        }

    switch (e.type())
    {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            if (fingers.size() == 0)
            {
                FingerInfo info;
                info.fingerId = 0;
                info.x = e.x();
                info.y = e.y();
                fingers.push_back(info);
            }

            if (!panning &&
                e.button() == SDL_BUTTON_MIDDLE ||
                (single_touch_pan && e.button() == SDL_BUTTON_LEFT))
            {
                panBegin((int)e.x(), (int)e.y(), 0.0, 0.0);
            }
        }
        break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            fingers.clear();
            if (e.button() == SDL_BUTTON_MIDDLE ||
                (single_touch_pan && e.button() == SDL_BUTTON_LEFT))
            {
                panEnd();
            }
        }
        break;
        case SDL_EVENT_MOUSE_MOTION:
        {
            if (fingers.size() > 0)
            {
                FingerInfo& info = fingers[0];
                info.x = e.x();
                info.y = e.y();
            }

            return panZoomProcess(*viewport);
            //if (panning)
            //    panDrag((int)e.x(), (int)e.y(), 0.0, 0.0);
        }
        break;
        case SDL_EVENT_MOUSE_WHEEL:
        {
            UNUSED(zoom_anchor_mouse);
            //if (zoom_anchor_mouse)
            //{
            //    // Zoom towards mouse position
            //    DVec2 before_zoom = toWorld(e.x(), e.y());
            //    setZoomX(zoomX() + (e.wheelY() / 10.0) * zoomX());
            //    setZoomY(zoomY() + (e.wheelY() / 10.0) * zoomY());
            //    DVec2 after_zoom = toWorld(e.x(), e.y());
            //    setPos(x() + (before_zoom.x - after_zoom.x), y() + (before_zoom.y - after_zoom.y));
            //    return panZoomProcess();
            //}
            //else
            {
                camera->setZoom(camera->zoom() + ((e.wheelY()/10.0) * wheel_zoom_rate) * camera->zoom());
                panZoomProcess(*viewport);
                return true;
            }
        }
        break;
    }
    return false;
}

void CameraNavigator::debugPrint(Viewport* ctx) const
{
    double pinch_zoom_factor = touchDist() / panDownTouchDist();

    //setFont(getDefaultFont());
    ctx->print() << "cam.position          = (" << camera->x() << ",  " << camera->y() << ")\n";
    ctx->print() << "cam.pan               = (" << camera->panX() << ",  " << camera->panY() << ")\n";
    ctx->print() << "cam.rotation          = " << camera->rotation() << "\n";
    ctx->print() << "cam.zoom              = (" << camera->zoomX() << ", " << camera->zoomY() << ")\n\n";
    ctx->print() << "--------------------------------------------------------------\n";
    ctx->print() << "pan_down_touch_x      = " << panDownTouchX() << "\n";
    ctx->print() << "pan_down_touch_y      = " << panDownTouchY() << "\n";
    ctx->print() << "pan_down_touch_dist   = " << panDownTouchDist() << "\n";
    ctx->print() << "pan_down_touch_angle  = " << panDownTouchAngle() << "\n";
    ctx->print() << "--------------------------------------------------------------\n";
    ctx->print() << "pan_beg_cam_x         = " << (double)panBegCamX() << "\n";
    ctx->print() << "pan_beg_cam_y         = " << (double)panBegCamY() << "\n";
    ctx->print() << "pan_beg_cam_zoom_x    = " << (double)panBegCamZoom() << "\n";
    ctx->print() << "pan_beg_cam_angle     = " << panBegCamAngle() << "\n";
    ctx->print() << "--------------------------------------------------------------\n";
    ctx->print() << "cam.touchAngle()      = " << touchAngle() << "\n";
    ctx->print() << "cam.touchDist()       = " << touchDist() << "\n";
    ctx->print() << "--------------------------------------------------------------\n";
    ctx->print() << "pinch_zoom_factor     = " << pinch_zoom_factor << "x\n";
    ctx->print() << "--------------------------------------------------------------\n";

    int i = 0;
    auto fingers = pressedFingers();
    for (auto finger : fingers)
    {
        ctx->print() << "Finger " << i << ": [id: " << finger.fingerId << "] - (" << finger.x << ", " << finger.y << ")\n";
        i++;
    }
}




BL_END_NS

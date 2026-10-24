# Input And Events

Use this page when adding mouse, touch, wheel, keyboard, or camera interaction.

## Event Ownership

Scene event handlers should check whether the event belongs to the scene/viewport before mutating state:

```cpp
if (!e.ownedBy(this))
    return;
```

For hover-sensitive behavior, pointer events expose `hoveredOver(this)`.

Overlapping visible Viewports are hit-tested from highest render order to lowest. Rendering uses ascending `zIndex()`; hit-testing reverses that order. `viewport.setPointerEvents(PointerEvents::None)` makes an overlay pass pointer input through to lower Viewports. Hidden Viewports are never hit-tested.

An active pointer capture remains owned after the pointer leaves its Viewport. Hiding or removing that Viewport cancels its focus, hover, and capture state before destruction, so event handlers must not retain raw Viewport pointers beyond their normal lifecycle.

## Event Types

`Event` owns its `EventData` payload and tracks focused/hovered viewport ownership. Copying an event also copies its text, file-drop data and keyboard name. The Viewport pointers remain borrowed and must not be used after their Viewport is removed. It exposes helpers such as:

- `isPointerEvent()`
- `isFingerEvent()`
- `isPressEvent()`
- `ownedBy(scene)`

Specialized handlers are usually clearer than switching on event types when the generated template provides them:

- `onEvent(Event e)`
- `onPointerDown(PointerEvent e)`
- `onPointerUp(PointerEvent e)`
- `onPointerMove(PointerEvent e)`
- `onWheel(PointerEvent e)`
- `onKeyDown(KeyEvent e)`
- `onKeyUp(KeyEvent e)`

Use `EventType::MOUSE_BUTTON_DOWN`, `EventType::FINGER_UP`, etc. for explicit type checks. Numeric event, key, scancode and button identifiers retain their existing values, so existing SDL constant comparisons continue to work in code that includes SDL. Public event/input headers do not include SDL.

`e.data()` returns a const reference to the owned payload. For example, a `DROP_FILE` event exposes the UTF-8 path as `e.data().drop.data`; a `TEXT_INPUT` event exposes `e.data().text.text`. There is no `e.sdl()` accessor.

The SDL host calls `copySDLEvent(...)` before enqueueing, while SDL-managed strings are still valid. Worker queues and headless schedules store `EventData` values. Conversion preserves mouse, wheel, touch, keyboard, text/IME and drop fields, plus window-event metadata. Other platform event kinds retain their numeric type and timestamp only; arbitrary SDL user pointers are not transported. `InputEmulation` remains an SDL-facing development helper and copies its source event immediately when scheduling it.

Payload conversion does not select a Viewport. The existing dispatcher still performs hit-testing, focus and capture routing in event order; callbacks and camera navigation receive those routed events. GUI input filtering and dispatch while the worker is paused follow the existing host path.

## Pointer Events

`PointerEvent` provides:

- `x()` and `y()` in viewport/surface coordinates
- `button()`
- `wheelY()`
- `fingerID()`
- ownership and hover helpers

Convert pointer positions through the camera chosen by the Scene. The event supplies the owning Viewport surface:

```cpp
DVec2 p = e.worldPosition(camera);
```

## Keyboard Events

`KeyEvent` provides:

- `keyCode()`
- `scanCode()`
- `keyMod()`
- `keyName()`

Keyboard names are captured with the input and remain valid for the lifetime of the copied `KeyEvent`. `KeyEvent` preserves the same ownership metadata as the generic event.

Use key events for explicit controls. Avoid adding controls for future interactions unless the user requested them.

## Mouse State

`MouseInfo` stores current pointer and button state:

- `client_x`, `client_y`
- `stage_x`, `stage_y`
- button states and helpers such as `buttonClicked` and `buttonDown`

Use `mouse->worldPosition(camera)` when continuous interaction needs world coordinates, after checking that `mouse->viewport` belongs to the intended Scene. Use event wrappers for discrete actions.

## Camera Navigation

For panning, zooming, and standard world navigation, prefer:

```cpp
if (e.ownedBy(this))
    navigator.handleWorldNavigation(e, true);
```

The navigator resolves projection through the event's owning Viewport; it does not bind the Camera to a Viewport.

Do not leave default camera navigation enabled when it would compete with the project's primary pointer/touch interaction. When a project's primary interaction is manipulating domain objects or locations directly with pointer/touch input, a drag, press, scroll, pinch, or release should first belong to that domain interaction. Use a fixed camera, explicit UI controls, or a clearly separated navigation mode unless camera movement is part of the requested experience.

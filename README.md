# RGFW Under the Hood: Raw Mouse Input and Mouse Locking
## Introduction 
When you create an application that locks the cursor, such as a game with a first-person camera, it's important to be able to disable the cursor.
This means locking the cursor in the middle of the screen and getting raw input. 

The only alternative to this method would be a hack that pulls the mouse back to the center of the window when this moves. However this is a hack so it can be buggy
and does not work on all OSes. Therefore, it's important to properly lock the mouse by using raw input. 

This tutorial explains how RGFW handles raw mouse input so you can understand it and/or implement it yourself. 

## Overview 
A quick overview of the steps required

1. lock cursor (if required)
2. center the cursor
3. enable raw input
4. handle raw input
5. disable raw input
6. unlock cursor

When the user asks RGFW to hold the cursor RGFW enables a bitmask that says the cursor is held.

```c
win->_winArgs |= RGFW_HOLD_MOUSE;
```

## Step 1 (Lock Cursor) 

On X11 the cursor should be locked by grabbing it via `XGrabPointer` 

```c
XGrabPointer(display, window, True, PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
```

This gives the window full control of the pointer, thereby locking it to the window. 

On Windows, ClipCursor is used to lock the cursor to a specific rect on the screen.
This means we need to find the window rectangle on the screen and then clip the mouse to that rectangle. 

```c
// first get the window size (the RGFW_window struct also includes this informaton, but using this ensures it's correct)
RECT clipRect;
GetClientRect(window, &clipRect);

// ClipCursor needs screen coords, not coords relative to the window
ClientToScreen(window, (POINT*) &clipRect.left);
ClientToScreen(window, (POINT*) &clipRect.right);

// now we can lock the cursor
ClipCursor(&clipRect);
```


On MacOS and Emscripten the function to enable rawinput also locks the cursor. So I'll get to it's function on step 4.

## Step 2 (center the cursor)
After the cursor is locked, it should be centered in the middle of the screen. 
This is the cursor is locked in the right place and won't mess with anything else. 

RGFW uses an RGFW function called `RGFW_window_moveMouse` to move the mouse in the middle of the window. 

On X11, XWrapPointer can be used to move the cursor to the center of the window 

```c
XWarpPointer(display, None, window, 0, 0, 0, 0, window_width / 2, window_height / 2);
```


On Windows, SetCursorPos is used

```c
SetCursorPos(window_x + (window_width / 2), window_y + (window_height / 2));
```

On MacOS, CGWarpMouseCursorPosition is used

```c
CGWarpMouseCursorPosition(window_x + (window_width / 2), window_y + (window_height / 2));
```

On Emscripten, RGFW does not move the mouse.

## Step 3 (enable raw input)

With X11, XI is used to enable raw input

```c
// mask for XI and set mouse for raw mouse input ("RawMotion")
unsigned char mask[XIMaskLen(XI_RawMotion)] = { 0 };
XISetMask(mask, XI_RawMotion);

// set up X1 struct
XIEventMask em;
em.deviceid = XIAllMasterDevices;
em.mask_len = sizeof(mask);
em.mask = mask;

// enable raw input using the structure
XISelectEvents(display, XDefaultRootWindow(display), &em, 1);
```


On windows, you need to set up the RAWINPUTDEVICE structure and enable it with `RegisterRawInputDevices`

```c
const RAWINPUTDEVICE id = { 0x01, 0x02, 0, win->src.window };
RegisterRawInputDevices(&id, 1, sizeof(id));
```

On MacOS you only need to run `CGAssociateMouseAndMouseCursorPosition`
This also locks the cursor by disassosiating the mouse cursor and the mouse movement 

```c
CGAssociateMouseAndMouseCursorPosition(0);
```

On emscripten you only need to request the user to lock the pointer 

```c
emscripten_request_pointerlock("#canvas", 1);
```

## Step 4 (handle raw input events)

These all happen during event loops


For X11, you need to handle the normal MotionNotify, manually converting the input to raw input.
You also need to handle GenericEvent, checking for raw mouse input events.

```c
switch (E.type) {
    (...)
		case MotionNotify:
            /* check if mouse hold is enabled */
			if ((win->_winArgs & RGFW_HOLD_MOUSE)) {
				/* convert E.xmotion to rawinput by substracting the previous point */
                win->event.point.x = win->_lastMousePoint.x - E.xmotion.x;
				win->event.point.y = win->_lastMousePoint.y - E.xmotion.y;
			
                // the mouse must be moved back to the center when it moves
                XWarpPointer(display, None, window, 0, 0, 0, 0, window_width / 2, window_height / 2);
            }
        
			break;

		case GenericEvent: {
			/* MotionNotify is used for mouse events if the mouse isn't held */                
			if (!(win->_winArgs & RGFW_HOLD_MOUSE)) {
            	XFreeEventData(win->src.display, &E.xcookie);
				break;
			}
			
            XGetEventData(win->src.display, &E.xcookie);
            if (E.xcookie.evtype == XI_RawMotion) {
				XIRawEvent *raw = (XIRawEvent *)E.xcookie.data;
				if (raw->valuators.mask_len == 0) {
					XFreeEventData(win->src.display, &E.xcookie);
					break;
				}

                double deltaX = 0.0f; 
				double deltaY = 0.0f;

                /* check if relative motion data exists where we think it does */
				if (XIMaskIsSet(raw->valuators.mask, 0) != 0)
					deltaX += raw->raw_values[0];
				if (XIMaskIsSet(raw->valuators.mask, 1) != 0)
					deltaY += raw->raw_values[1];
                
                // the mouse must be moved back to the center when it moves
                XWarpPointer(display, None, window, 0, 0, 0, 0, window_width / 2, window_height / 2);
				win->event.point = RGFW_POINT((u32)-deltaX, (u32)-deltaY);
            }

            XFreeEventData(win->src.display, &E.xcookie);
			break;
		}
```

On windows you only need to handle WM_INPUT events and check for raw motion input

```c
switch (msg.message) {
            (...)
            case WM_INPUT: {
                /* check if the mouse is being held */
				if (!(win->_winArgs & RGFW_HOLD_MOUSE))
					break;
				
                /* get raw data as an array */
				unsigned size = sizeof(RAWINPUT);
				static RAWINPUT raw[sizeof(RAWINPUT)];
				GetRawInputData((HRAWINPUT)msg.lParam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER));
                
                // make sure raw data is valid 
				if (raw->header.dwType != RIM_TYPEMOUSE || (raw->data.mouse.lLastX == 0 && raw->data.mouse.lLastY == 0) )
					break;
				
                // the data is flipped  
				win->event.point.x = -raw->data.mouse.lLastX;
				win->event.point.y = -raw->data.mouse.lLastY;
				break;
			}
```

On macOS you can just check mouse input as normal, while using deltaX and deltaY to fetch and flip the mouse point 

```c
switch (objc_msgSend_uint(e, sel_registerName("type"))) {
			case NSEventTypeLeftMouseDragged:
			case NSEventTypeOtherMouseDragged:
			case NSEventTypeRightMouseDragged:
			case NSEventTypeMouseMoved:
				if ((win->_winArgs & RGFW_HOLD_MOUSE) == 0) // if the mouse is not held
                    break;
                
                NSPoint p;
				p.x = ((CGFloat(*)(id, SEL))abi_objc_msgSend_fpret)(e, sel_registerName("deltaX"));
				p.y = ((CGFloat(*)(id, SEL))abi_objc_msgSend_fpret)(e, sel_registerName("deltaY"));
				
                // the raw input must be flipped for macOS as well, and casted for RGFW's event data
				win->event.point = RGFW_POINT((u32) -p.x, (u32) -p.y));
```

On Emscripten the mouse events can be checked as they normally are, except we're going to use and flip e->movementX/Y

```c
EM_BOOL Emscripten_on_mousemove(int eventType, const EmscriptenMouseEvent* e, void* userData) {
	if ((RGFW_root->_winArgs & RGFW_HOLD_MOUSE) == 0) // if the mouse is not held
        return

	// the raw input must be flipped for emscripten as well
    RGFW_point p = RGFW_POINT(-e->movementX, -e->movementY);
}
```

## Step 5 (disable raw input)
Whenever you're done with the mouse, you'll want to get back to normal mouse input. 

First, RGFW disables the bitmask that.

```c
win->_winArgs ^= RGFW_HOLD_MOUSE;
```

In X11, first you have to create a structure with a blank mouse and asks for the `XIAllMasterDevices` devices.
This will disable raw input.

```c
unsigned char mask[] = { 0 };
XIEventMask em;
em.deviceid = XIAllMasterDevices;

em.mask_len = sizeof(mask);
em.mask = mask;
XISelectEvents(win->src.display, XDefaultRootWindow(win->src.display), &em, 1);
```

For Windows, you pass a rawinput device stucture with `RIDEV_REMOVE` to disable the rawinput.

```c
const RAWINPUTDEVICE id = { 0x01, 0x02, RIDEV_REMOVE, NULL };
RegisterRawInputDevices(&id, 1, sizeof(id));
```

On MacOS and Emscripten, unlocking the cursor also disables rawinput.

## Step 6 (unlock cursor)

On X11, `XUngrabPoint` can can used to unlock the cursor.

```c
XUngrabPointer(display, CurrentTime);
```

On Windows, pass a NULL rectangle pointer to unlock the cursor.

```c
ClipCursor(NULL);
```

On MacOS, assosiating the mouse cursor and the mouse movement will disable rawinput and unlock the cursor

```c
CGAssociateMouseAndMouseCursorPosition(1);
```

On Emscripten, exiting the pointerlock will unlock the cursor and disable raw input.

```c
emscripten_exit_pointerlock();
```

// This can be compiled with 
// gcc x11.c -lX11 -lXi

#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
#include <X11/extensions/XInput2.h>

int main(void) {
    unsigned int window_width = 200;
	unsigned int window_height = 200;

	Display* display = XOpenDisplay(NULL);  
    Window window = XCreateSimpleWindow(display, RootWindow(display, DefaultScreen(display)), 400, 400, window_width, window_height, 1,
                                 BlackPixel(display, DefaultScreen(display)), WhitePixel(display, DefaultScreen(display)));
 
    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XMapWindow(display, window);
	
	XGrabPointer(display, window, True, PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	
	XWarpPointer(display, None, window, 0, 0, 0, 0, window_width / 2, window_height / 2);

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

	Bool rawInput = True;
	XPoint point;
	XPoint _lastMousePoint;

	XEvent event;

    for (;;) {
        XNextEvent(display, &event);
		
		switch (event.type) {
			case MotionNotify:
				/* check if mouse hold is enabled */
				if (rawInput) {
					/* convert E.xmotion to rawinput by substracting the previous point */
					point.x = _lastMousePoint.x - event.xmotion.x;
					point.y = _lastMousePoint.y - event.xmotion.y;
					printf("rawinput %i %i\n", point.x, point.y);
					XWarpPointer(display, None, window, 0, 0, 0, 0, window_width / 2, window_height / 2);
				}
        
				break;

			case GenericEvent: {
				/* MotionNotify is used for mouse events if the mouse isn't held */                
				if (rawInput == False) {
					XFreeEventData(display, &event.xcookie);
					break;
				}
			
				XGetEventData(display, &event.xcookie);
				if (event.xcookie.evtype == XI_RawMotion) {
					XIRawEvent *raw = (XIRawEvent *)event.xcookie.data;
					if (raw->valuators.mask_len == 0) {
						XFreeEventData(display, &event.xcookie);
						break;
					}

					double deltaX = 0.0f; 
					double deltaY = 0.0f;

					/* check if relative motion data exists where we think it does */
					if (XIMaskIsSet(raw->valuators.mask, 0) != 0)
						deltaX += raw->raw_values[0];
					if (XIMaskIsSet(raw->valuators.mask, 1) != 0)
						deltaY += raw->raw_values[1];

					point = (XPoint){-deltaX, -deltaY};
					XWarpPointer(display, None, window, 0, 0, 0, 0, window_width / 2, window_height / 2);

					printf("rawinput %i %i\n", point.x, point.y);
				}	

				XFreeEventData(display, &event.xcookie);
				break;
			}
			case KeyPress:
				if (rawInput == False)
					break;

				unsigned char mask[] = { 0 };
				XIEventMask em;
				em.deviceid = XIAllMasterDevices;

				em.mask_len = sizeof(mask);
				em.mask = mask;
				XISelectEvents(display, XDefaultRootWindow(display), &em, 1);
				XUngrabPointer(display, CurrentTime);

				printf("Raw input disabled\n");
				break;
			default: break;
		}
    }
 
    XCloseDisplay(display);
 }

// compile with gcc winapi.c

#include <windows.h>

#include <stdio.h>
#include <stdint.h>
#include <assert.h>

int main() {
	WNDCLASS wc = {0};
	wc.lpfnWndProc   = DefWindowProc; // Default window procedure
	wc.hInstance     = GetModuleHandle(NULL);
	wc.lpszClassName = "SampleWindowClass";
	
	RegisterClass(&wc);
	
	int window_width = 300;
	int window_height = 300;
	int window_x = 400;
	int window_y = 400;
	
	HWND hwnd = CreateWindowA(wc.lpszClassName, "Sample Window", 0,
			window_x, window_y, window_width, window_height,
			NULL, NULL, wc.hInstance, NULL);
	
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	
	// first get the window size (the RGFW_window struct also includes this informaton, but using this ensures it's correct)
	RECT clipRect;
	GetClientRect(hwnd, &clipRect);
	
	// ClipCursor needs screen coords, not coords relative to the window
	ClientToScreen(hwnd, (POINT*) &clipRect.left);
	ClientToScreen(hwnd, (POINT*) &clipRect.right);
	
	// now we can lock the cursor
	ClipCursor(&clipRect);
	
	SetCursorPos(window_x + (window_width / 2), window_y + (window_height / 2));	
	const RAWINPUTDEVICE id = { 0x01, 0x02, 0, hwnd };
	RegisterRawInputDevices(&id, 1, sizeof(id));
	
	MSG msg;
	
	BOOL holdMouse = TRUE;
	
	BOOL running = TRUE;
	
	POINT point;
	
	while (running) {
		if (PeekMessageA(&msg, hwnd, 0u, 0u, PM_REMOVE)) {
			switch (msg.message) {
				case WM_CLOSE:
				case WM_QUIT:
					running = FALSE;
					break;
				case WM_INPUT: {
					/* check if the mouse is being held */
					if (holdMouse == FALSE)
						break;
					
					/* get raw data as an array */
					unsigned size = sizeof(RAWINPUT);
					static RAWINPUT raw[sizeof(RAWINPUT)];
					GetRawInputData((HRAWINPUT)msg.lParam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER));
					
					// make sure raw data is valid 
					if (raw->header.dwType != RIM_TYPEMOUSE || (raw->data.mouse.lLastX == 0 && raw->data.mouse.lLastY == 0) )
						break;
					
					// the data is flipped  
					point.x = raw->data.mouse.lLastX;
					point.y = raw->data.mouse.lLastY;
					printf("raw input: %i %i\n", point.x, point.y);
					break;
				}
				case WM_KEYDOWN:
					if (holdMouse == FALSE)
						break;
					
					const RAWINPUTDEVICE id = { 0x01, 0x02, RIDEV_REMOVE, NULL };
					RegisterRawInputDevices(&id, 1, sizeof(id));
					ClipCursor(NULL);
					
					printf("rawinput disabled\n");
					holdMouse = FALSE;
					break;
	
				default: break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		running = IsWindow(hwnd);
	}
	
	DestroyWindow(hwnd);
	return 0;
}

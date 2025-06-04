#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>

#ifdef _WIN32
    #include <windows.h>
    #include <winuser.h>
    #define SLEEP_MS(x) Sleep(x)
#else
    #include <X11/Xlib.h>
    #include <X11/extensions/XTest.h>
    #include <X11/keysym.h>
    #define SLEEP_MS(x) usleep((x)*1000)
#endif

// OpenCV headers (assuming OpenCV is installed)
#ifdef OPENCV_AVAILABLE
    #include <opencv2/opencv.hpp>
    #include <opencv2/objdetect.hpp>
    #include <opencv2/imgproc.hpp>
#endif

// Configuration constants
#define MAX_CALIBRATION_POINTS 9
#define BLINK_THRESHOLD 0.3
#define GAZE_SMOOTHING_FACTOR 0.7
#define CLICK_HOLD_TIME 500
#define DOUBLE_BLINK_TIMEOUT 800
#define SCREEN_MARGIN 50

// Eye tracking data structures
typedef struct {
    double x, y;
} Point2D;

typedef struct {
    Point2D center;
    double radius;
    bool detected;
} Eye;

typedef struct {
    Eye left_eye;
    Eye right_eye;
    Point2D gaze_point;
    bool is_blinking;
    double blink_confidence;
    time_t last_blink_time;
    int blink_count;
} EyeData;

typedef struct {
    Point2D screen_points[MAX_CALIBRATION_POINTS];
    Point2D gaze_points[MAX_CALIBRATION_POINTS];
    int point_count;
    bool is_calibrated;
    double transform_matrix[6]; // Affine transformation
} CalibrationData;

typedef struct {
    bool mouse_control;
    bool keyboard_control;
    bool gesture_control;
    bool voice_control;
    bool accessibility_mode;
    int sensitivity;
    int smoothing_level;
} DriverSettings;

// Global variables
static EyeData current_eye_data = {0};
static CalibrationData calibration = {0};
static DriverSettings settings = {
    .mouse_control = true,
    .keyboard_control = true,
    .gesture_control = true,
    .voice_control = false,
    .accessibility_mode = false,
    .sensitivity = 5,
    .smoothing_level = 3
};

#ifdef _WIN32
static POINT screen_size;
#else
static Display *display;
static int screen_width, screen_height;
#endif

// Function prototypes
int initialize_driver(void);
void cleanup_driver(void);
int detect_eyes(unsigned char *frame_data, int width, int height);
void calculate_gaze_point(void);
void apply_calibration_transform(Point2D *gaze_point);
void move_mouse_cursor(Point2D target);
void handle_blink_gestures(void);
void handle_eye_gestures(void);
int perform_calibration(void);
void load_settings(const char *config_file);
void save_settings(const char *config_file);
void log_event(const char *message);

// System-specific mouse and keyboard control
#ifdef _WIN32
void system_move_mouse(int x, int y) {
    SetCursorPos(x, y);
}

void system_click_mouse(int button) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    
    if (button == 1) { // Left click
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &input, sizeof(INPUT));
        SLEEP_MS(50);
        input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &input, sizeof(INPUT));
    } else if (button == 2) { // Right click
        input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        SendInput(1, &input, sizeof(INPUT));
        SLEEP_MS(50);
        input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void system_send_key(int keycode) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = keycode;
    
    SendInput(1, &input, sizeof(INPUT)); // Key down
    SLEEP_MS(50);
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT)); // Key up
}

void get_screen_size(void) {
    screen_size.x = GetSystemMetrics(SM_CXSCREEN);
    screen_size.y = GetSystemMetrics(SM_CYSCREEN);
}

#else
void system_move_mouse(int x, int y) {
    XWarpPointer(display, None, DefaultRootWindow(display), 0, 0, 0, 0, x, y);
    XFlush(display);
}

void system_click_mouse(int button) {
    XTestFakeButtonEvent(display, button, True, CurrentTime);
    XFlush(display);
    SLEEP_MS(50);
    XTestFakeButtonEvent(display, button, False, CurrentTime);
    XFlush(display);
}

void system_send_key(KeySym key) {
    KeyCode keycode = XKeysymToKeycode(display, key);
    XTestFakeKeyEvent(display, keycode, True, CurrentTime);
    XFlush(display);
    SLEEP_MS(50);
    XTestFakeKeyEvent(display, keycode, False, CurrentTime);
    XFlush(display);
}

void get_screen_size(void) {
    Screen *screen = DefaultScreenOfDisplay(display);
    screen_width = WidthOfScreen(screen);
    screen_height = HeightOfScreen(screen);
}
#endif

// Initialize the driver
int initialize_driver(void) {
    printf("Initializing Eye Control Driver...\n");
    
    // Initialize system-specific components
#ifdef _WIN32
    get_screen_size();
    printf("Screen resolution: %ldx%ld\n", screen_size.x, screen_size.y);
#else
    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Error: Cannot open X display\n");
        return -1;
    }
    get_screen_size();
    printf("Screen resolution: %dx%d\n", screen_width, screen_height);
#endif

    // Initialize eye tracking components
    memset(&current_eye_data, 0, sizeof(EyeData));
    memset(&calibration, 0, sizeof(CalibrationData));
    
    // Load configuration
    load_settings("eye_control_config.txt");
    
    printf("Driver initialized successfully!\n");
    return 0;
}

// Cleanup resources
void cleanup_driver(void) {
    printf("Cleaning up Eye Control Driver...\n");
    
#ifndef _WIN32
    if (display) {
        XCloseDisplay(display);
    }
#endif
    
    save_settings("eye_control_config.txt");
    printf("Driver cleanup complete.\n");
}

// Simplified eye detection (would use OpenCV in real implementation)
int detect_eyes(unsigned char *frame_data, int width, int height) {
    // This is a placeholder for actual computer vision processing
    // In a real implementation, this would use OpenCV's face/eye detection
    
    static double simulation_time = 0;
    simulation_time += 0.1;
    
    // Simulate eye detection with moving pattern
    current_eye_data.left_eye.center.x = width * 0.35 + sin(simulation_time) * 20;
    current_eye_data.left_eye.center.y = height * 0.4 + cos(simulation_time * 0.7) * 15;
    current_eye_data.left_eye.radius = 15.0;
    current_eye_data.left_eye.detected = true;
    
    current_eye_data.right_eye.center.x = width * 0.65 + sin(simulation_time + 1) * 20;
    current_eye_data.right_eye.center.y = height * 0.4 + cos(simulation_time * 0.7 + 1) * 15;
    current_eye_data.right_eye.radius = 15.0;
    current_eye_data.right_eye.detected = true;
    
    // Simulate blinking
    double blink_phase = sin(simulation_time * 2);
    current_eye_data.is_blinking = blink_phase > 0.8;
    current_eye_data.blink_confidence = fmax(0, blink_phase);
    
    return current_eye_data.left_eye.detected && current_eye_data.right_eye.detected;
}

// Calculate gaze point from eye positions
void calculate_gaze_point(void) {
    if (!current_eye_data.left_eye.detected || !current_eye_data.right_eye.detected) {
        return;
    }
    
    // Average the eye positions for gaze estimation
    Point2D raw_gaze;
    raw_gaze.x = (current_eye_data.left_eye.center.x + current_eye_data.right_eye.center.x) / 2.0;
    raw_gaze.y = (current_eye_data.left_eye.center.y + current_eye_data.right_eye.center.y) / 2.0;
    
    // Apply smoothing
    static Point2D previous_gaze = {0, 0};
    current_eye_data.gaze_point.x = GAZE_SMOOTHING_FACTOR * previous_gaze.x + 
                                   (1.0 - GAZE_SMOOTHING_FACTOR) * raw_gaze.x;
    current_eye_data.gaze_point.y = GAZE_SMOOTHING_FACTOR * previous_gaze.y + 
                                   (1.0 - GAZE_SMOOTHING_FACTOR) * raw_gaze.y;
    
    previous_gaze = current_eye_data.gaze_point;
    
    // Apply calibration if available
    if (calibration.is_calibrated) {
        apply_calibration_transform(&current_eye_data.gaze_point);
    }
}

// Apply calibration transformation to gaze point
void apply_calibration_transform(Point2D *gaze_point) {
    if (!calibration.is_calibrated) return;
    
    // Apply affine transformation
    double x = gaze_point->x;
    double y = gaze_point->y;
    
    gaze_point->x = calibration.transform_matrix[0] * x + 
                   calibration.transform_matrix[1] * y + 
                   calibration.transform_matrix[2];
    gaze_point->y = calibration.transform_matrix[3] * x + 
                   calibration.transform_matrix[4] * y + 
                   calibration.transform_matrix[5];
}

// Move mouse cursor to target position
void move_mouse_cursor(Point2D target) {
    if (!settings.mouse_control) return;
    
#ifdef _WIN32
    int screen_x = (int)(target.x * screen_size.x);
    int screen_y = (int)(target.y * screen_size.y);
    
    // Apply screen margins
    screen_x = fmax(SCREEN_MARGIN, fmin(screen_size.x - SCREEN_MARGIN, screen_x));
    screen_y = fmax(SCREEN_MARGIN, fmin(screen_size.y - SCREEN_MARGIN, screen_y));
#else
    int screen_x = (int)(target.x * screen_width);
    int screen_y = (int)(target.y * screen_height);
    
    // Apply screen margins
    screen_x = fmax(SCREEN_MARGIN, fmin(screen_width - SCREEN_MARGIN, screen_x));
    screen_y = fmax(SCREEN_MARGIN, fmin(screen_height - SCREEN_MARGIN, screen_y));
#endif
    
    system_move_mouse(screen_x, screen_y);
}

// Handle blink-based gestures
void handle_blink_gestures(void) {
    static time_t last_blink = 0;
    static int consecutive_blinks = 0;
    time_t current_time = time(NULL);
    
    if (current_eye_data.is_blinking && current_eye_data.blink_confidence > BLINK_THRESHOLD) {
        if (current_time - last_blink > 1) { // Reset if too much time passed
            consecutive_blinks = 0;
        }
        
        last_blink = current_time;
        consecutive_blinks++;
        
        // Single blink - left click
        if (consecutive_blinks == 1) {
            SLEEP_MS(200); // Wait to see if there's another blink
            if (consecutive_blinks == 1) {
                system_click_mouse(1);
                log_event("Single blink - Left click");
            }
        }
        // Double blink - right click
        else if (consecutive_blinks == 2) {
            system_click_mouse(2);
            log_event("Double blink - Right click");
            consecutive_blinks = 0;
        }
        // Triple blink - scroll mode toggle
        else if (consecutive_blinks == 3) {
            log_event("Triple blink - Scroll mode toggle");
            consecutive_blinks = 0;
        }
    }
    
    // Reset blink counter after timeout
    if (current_time - last_blink > 2) {
        consecutive_blinks = 0;
    }
}

// Handle eye movement gestures
void handle_eye_gestures(void) {
    static Point2D last_gaze = {0, 0};
    static time_t gesture_start = 0;
    
    // Calculate movement velocity
    double dx = current_eye_data.gaze_point.x - last_gaze.x;
    double dy = current_eye_data.gaze_point.y - last_gaze.y;
    double velocity = sqrt(dx*dx + dy*dy);
    
    // Detect rapid eye movements for gestures
    if (velocity > 0.1) { // Threshold for gesture detection
        time_t current_time = time(NULL);
        
        if (gesture_start == 0) {
            gesture_start = current_time;
        }
        
        // Horizontal swipe gestures
        if (fabs(dx) > fabs(dy) && fabs(dx) > 0.05) {
            if (dx > 0) {
                // Right swipe - Next tab/page
#ifdef _WIN32
                system_send_key(VK_TAB);
#else
                system_send_key(XK_Tab);
#endif
                log_event("Right swipe - Next tab");
            } else {
                // Left swipe - Previous tab/page
#ifdef _WIN32
                system_send_key(VK_TAB); // Would need Shift+Tab in real implementation
#else
                system_send_key(XK_Tab); // Would need Shift+Tab in real implementation
#endif
                log_event("Left swipe - Previous tab");
            }
        }
        
        // Vertical swipe gestures
        if (fabs(dy) > fabs(dx) && fabs(dy) > 0.05) {
            if (dy > 0) {
                // Down swipe - Scroll down
                log_event("Down swipe - Scroll down");
            } else {
                // Up swipe - Scroll up
                log_event("Up swipe - Scroll up");
            }
        }
    } else {
        gesture_start = 0;
    }
    
    last_gaze = current_eye_data.gaze_point;
}

// Perform calibration procedure
int perform_calibration(void) {
    printf("Starting calibration procedure...\n");
    printf("Look at each point when prompted and blink to confirm.\n");
    
    // Define calibration points (3x3 grid)
    Point2D cal_points[9] = {
        {0.1, 0.1}, {0.5, 0.1}, {0.9, 0.1},
        {0.1, 0.5}, {0.5, 0.5}, {0.9, 0.5},
        {0.1, 0.9}, {0.5, 0.9}, {0.9, 0.9}
    };
    
    for (int i = 0; i < 9; i++) {
        printf("Look at calibration point %d/9 (%.1f, %.1f) and blink...\n", 
               i+1, cal_points[i].x, cal_points[i].y);
        
        // Wait for blink confirmation
        bool confirmed = false;
        time_t start_time = time(NULL);
        
        while (!confirmed && (time(NULL) - start_time) < 10) {
            // Simulate frame processing
            unsigned char dummy_frame[640*480] = {0};
            detect_eyes(dummy_frame, 640, 480);
            calculate_gaze_point();
            
            if (current_eye_data.is_blinking) {
                calibration.screen_points[i] = cal_points[i];
                calibration.gaze_points[i] = current_eye_data.gaze_point;
                confirmed = true;
                printf("Point %d confirmed!\n", i+1);
                SLEEP_MS(1000); // Wait before next point
            }
            
            SLEEP_MS(100);
        }
        
        if (!confirmed) {
            printf("Calibration timeout for point %d\n", i+1);
            return -1;
        }
    }
    
    calibration.point_count = 9;
    calibration.is_calibrated = true;
    
    // Calculate transformation matrix (simplified - would use proper least squares fitting)
    for (int i = 0; i < 6; i++) {
        calibration.transform_matrix[i] = (i < 2) ? 1.0 : 0.0;
    }
    
    printf("Calibration completed successfully!\n");
    return 0;
}

// Load settings from configuration file
void load_settings(const char *config_file) {
    FILE *file = fopen(config_file, "r");
    if (!file) {
        printf("Config file not found, using defaults.\n");
        return;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "mouse_control=", 14) == 0) {
            settings.mouse_control = (atoi(line + 14) != 0);
        } else if (strncmp(line, "sensitivity=", 12) == 0) {
            settings.sensitivity = atoi(line + 12);
        } else if (strncmp(line, "smoothing_level=", 16) == 0) {
            settings.smoothing_level = atoi(line + 16);
        }
        // Add more settings as needed
    }
    
    fclose(file);
    printf("Settings loaded from %s\n", config_file);
}

// Save settings to configuration file
void save_settings(const char *config_file) {
    FILE *file = fopen(config_file, "w");
    if (!file) {
        printf("Warning: Could not save settings to %s\n", config_file);
        return;
    }
    
    fprintf(file, "mouse_control=%d\n", settings.mouse_control);
    fprintf(file, "keyboard_control=%d\n", settings.keyboard_control);
    fprintf(file, "gesture_control=%d\n", settings.gesture_control);
    fprintf(file, "sensitivity=%d\n", settings.sensitivity);
    fprintf(file, "smoothing_level=%d\n", settings.smoothing_level);
    
    fclose(file);
    printf("Settings saved to %s\n", config_file);
}

// Log events for debugging and analysis
void log_event(const char *message) {
    time_t now = time(NULL);
    printf("[%s] %s\n", ctime(&now), message);
    
    // Optional: Write to log file
    FILE *log_file = fopen("eye_control.log", "a");
    if (log_file) {
        fprintf(log_file, "[%s] %s\n", ctime(&now), message);
        fclose(log_file);
    }
}

// Main processing loop
void run_eye_control_loop(void) {
    printf("Starting eye control loop...\n");
    printf("Press Ctrl+C to exit.\n");
    
    // Simulate camera frame data
    unsigned char frame_data[640 * 480] = {0};
    int frame_count = 0;
    
    while (1) {
        frame_count++;
        
        // Process current frame
        if (detect_eyes(frame_data, 640, 480)) {
            calculate_gaze_point();
            
            // Move mouse cursor based on gaze
            if (settings.mouse_control) {
                move_mouse_cursor(current_eye_data.gaze_point);
            }
            
            // Handle gestures
            if (settings.gesture_control) {
                handle_blink_gestures();
                handle_eye_gestures();
            }
        }
        
        // Print status every 100 frames
        if (frame_count % 100 == 0) {
            printf("Frame %d: Gaze(%.2f, %.2f) Eyes:%s Blink:%s\n",
                   frame_count,
                   current_eye_data.gaze_point.x,
                   current_eye_data.gaze_point.y,
                   (current_eye_data.left_eye.detected && current_eye_data.right_eye.detected) ? "OK" : "LOST",
                   current_eye_data.is_blinking ? "YES" : "NO");
        }
        
        SLEEP_MS(33); // ~30 FPS processing
    }
}

// Interactive menu system
void show_menu(void) {
    printf("\n=== Eye Control Driver Menu ===\n");
    printf("1. Start Eye Control\n");
    printf("2. Calibrate Eye Tracking\n");
    printf("3. Settings\n");
    printf("4. Test Eye Detection\n");
    printf("5. View Logs\n");
    printf("6. Exit\n");
    printf("Choice: ");
}

void settings_menu(void) {
    int choice;
    do {
        printf("\n=== Settings ===\n");
        printf("1. Mouse Control: %s\n", settings.mouse_control ? "ON" : "OFF");
        printf("2. Keyboard Control: %s\n", settings.keyboard_control ? "ON" : "OFF");
        printf("3. Gesture Control: %s\n", settings.gesture_control ? "ON" : "OFF");
        printf("4. Sensitivity: %d\n", settings.sensitivity);
        printf("5. Smoothing Level: %d\n", settings.smoothing_level);
        printf("6. Back to Main Menu\n");
        printf("Choice: ");
        
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input!\n");
            continue;
        }
        
        switch (choice) {
            case 1:
                settings.mouse_control = !settings.mouse_control;
                break;
            case 2:
                settings.keyboard_control = !settings.keyboard_control;
                break;
            case 3:
                settings.gesture_control = !settings.gesture_control;
                break;
            case 4:
                printf("Enter sensitivity (1-10): ");
                scanf("%d", &settings.sensitivity);
                break;
            case 5:
                printf("Enter smoothing level (1-10): ");
                scanf("%d", &settings.smoothing_level);
                break;
        }
    } while (choice != 6);
}

// Main function
int main(void) {
    printf("Eye-Controlled Computer Driver v2.0\n");
    printf("===================================\n");
    
    if (initialize_driver() != 0) {
        fprintf(stderr, "Failed to initialize driver!\n");
        return 1;
    }
    
    int choice;
    do {
        show_menu();
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input! Please enter a number.\n");
            continue;
        }
        
        switch (choice) {
            case 1:
                run_eye_control_loop();
                break;
            case 2:
                perform_calibration();
                break;
            case 3:
                settings_menu();
                break;
            case 4:
                printf("Testing eye detection for 10 seconds...\n");
                for (int i = 0; i < 300; i++) {
                    unsigned char test_frame[640*480] = {0};
                    detect_eyes(test_frame, 640, 480);
                    if (i % 30 == 0) {
                        printf("Eyes detected: %s\n", 
                               (current_eye_data.left_eye.detected && current_eye_data.right_eye.detected) ? "Yes" : "No");
                    }
                    SLEEP_MS(33);
                }
                break;
            case 5:
                printf("Check eye_control.log file for detailed logs.\n");
                break;
            case 6:
                printf("Exiting...\n");
                break;
            default:
                printf("Invalid choice! Please try again.\n");
        }
    } while (choice != 6);
    
    cleanup_driver();
    return 0;
}

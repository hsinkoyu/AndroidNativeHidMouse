#include <stdio.h>
#include <linux/input.h>
#include <fcntl.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>

/***************************

                 720
	+--------------------+------+
	|					 | [C]	|
	|					 +------+
	|							|
	|							|
	|							|
	|							|
	|			[D]				|
	|							|1280
	|							|
	|							|
	|							|
	|							|
	|							|
	+-------------+-------------+
	|			  |				|
	|	  [A]	  |		[B]		| 
	+-------------+-------------+
							 

[A, D]: Tapping in this area makes a left-button mouse click
[B]: Tapping in this area makes a right-button mouse click
[C]: Move finger to this area to exit mouse function
[A, B, D]: Mouse movement area
[A, B, D]: Touch and hold your finger for 1 second in this area to make a
           left-button mouse pressed (vibration alert). Move your finger to have a drag 
           action. Drop action is made once your finger is off the touchpad.
***************************/
#define LEFT_BUTTON_RECT_START_X	0
#define LEFT_BUTTON_RECT_START_Y	1000
#define LEFT_BUTTON_RECT_END_X	360
#define LEFT_BUTTON_RECT_END_Y	1280

#define RIGHT_BUTTON_RECT_START_X	361
#define RIGHT_BUTTON_RECT_START_Y	1000
#define RIGHT_BUTTON_RECT_END_X	720
#define RIGHT_BUTTON_RECT_END_Y	1280

#define EXIT_MOUSE_FUNCTION_RECT_START_X	640
#define EXIT_MOUSE_FUNCTION_RECT_START_Y	0
#define EXIT_MOUSE_FUNCTION_RECT_END_X	720
#define EXIT_MOUSE_FUNCTION_RECT_END_Y	80

typedef enum _finger_action {
	FINGER_OFF,
	FINGER_CONTACT,
	FINGER_CONTACT_POSITION_INITIATED,
	FINGER_MOVE
} FingerAction;

enum _finger_init_position_area {
	TP_LEFT_BUTTON, /* Touchpad Left Button */
	TP_RIGHT_BUTTON, /* Touchpad Right Button */
	TP_TOUCH /* Touchpad Touch */
};

typedef struct _finger_state {
	__s32 mt_position_x; /* Center X ellipse position */
	__s32 mt_position_y; /* Center Y ellipse position */
	__s32 mt_tracking_id; /* Unique ID of initiated contact */

	FingerAction action;
	int initPosArea; /* finger init position area */
	timer_t holdTimer;
	bool isHold;
	bool isEnabled; /* Currently we only support one finger control in one area */
} FingerState;

#define MT_NUMBER_OF_SLOTS 10

static FingerState gFingers[MT_NUMBER_OF_SLOTS];
static FingerState *gWorkingFinger = &gFingers[0];


static int hidg_mouse;
static char *hidg_mouse_device = "/dev/hidg0";

static FILE *vibrator = 0;
static char *vibrator_file = "/sys/class/timed_output/vibrator/enable";

static bool isLeftButtonHold(void) {
	int i;
	bool ret = false;

	for (i = 0; i < MT_NUMBER_OF_SLOTS; i++) {
		if (gFingers[i].isEnabled && gFingers[i].initPosArea == TP_LEFT_BUTTON && gFingers[i].isHold) {
			ret = true;
			break;
		}
	}

	return ret;
}

static void sendTouchHoldReport(void) {
	char report[3] = {0x01, 0, 0};

	write(hidg_mouse, report, sizeof(report));
}

static void sendFingerOffReport(void) {
	char report[3] = {0};

	write(hidg_mouse, report, sizeof(report));
}

static void sendMoveReport(char x, char y) {
	char report[3];

	if (!gWorkingFinger->isEnabled) {
		return;
	}

	if ((gWorkingFinger->initPosArea == TP_LEFT_BUTTON || gWorkingFinger->initPosArea == TP_RIGHT_BUTTON) &&
		gWorkingFinger->isHold) {
		return;
	}

	report[0] = 0;
	if (isLeftButtonHold()) {
		report[0] |= 0x01;
	}

	/* report[1]:	relative x movement
	   report[2]:	relative y movement
	*/
	report[1] = x;
	report[2] = y;

	write(hidg_mouse, report, sizeof(report));
}

static void sendClickReport(char button) {
	char report[3];

	report[1] = 0;
	report[2] = 0;

	/* report[0]:
	   bit7-bit3:	?
	   bit2:		middle button
	   bit1:		right button
	   bit0:		left button

	   bit set (1) indicates pressed
	   bit clear (0) indicates released
	*/
	report[0] = button; /* button pressed */
	write(hidg_mouse, report, sizeof(report));
	
	report[0] = 0x00; /* button released */
	write(hidg_mouse, report, sizeof(report));
}

static void vibrate(void) {
	if (vibrator) {
		fputs("100", vibrator);
		fflush(vibrator);
	}
}

static void stopTouchHoldTimer(timer_t *timer) {
	timer_delete(*timer);
}

static void touchHoldHandler(sigval_t v) {
	FingerState *finger = v.sival_ptr;
		
	if (FINGER_CONTACT_POSITION_INITIATED == finger->action) {
		finger->isHold = true;
		/* TODO: call this function in main thread? */
		if (finger->initPosArea == TP_LEFT_BUTTON || finger->initPosArea == TP_RIGHT_BUTTON) {
			sendTouchHoldReport();
			vibrate();
		}
	}
	stopTouchHoldTimer(&finger->holdTimer);
}

static int startTouchHoldTimer(timer_t *timer, int seconds, void *param) {
	struct sigevent se;
	struct itimerspec ts;

	memset(&se, 0, sizeof(se));
	se.sigev_notify = SIGEV_THREAD;
	se.sigev_notify_function = touchHoldHandler;
	se.sigev_value.sival_ptr = param;

	if (timer_create(CLOCK_REALTIME, &se, timer) < 0) {
	    return -1;
	}	
	ts.it_value.tv_sec = seconds;
	ts.it_value.tv_nsec = 0;
	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = 0;
	if (timer_settime(*timer, 0, &ts, NULL) < 0) {
	    return -1;
	}

	return 0;
}

static int decideInitPosArea(__s32 x, __s32 y) {
	int position;

	if ((x >= LEFT_BUTTON_RECT_START_X) &&
		(y >= LEFT_BUTTON_RECT_START_Y) &&
		(x <= LEFT_BUTTON_RECT_END_X) &&
		(y <= LEFT_BUTTON_RECT_END_Y)) {
		position = TP_LEFT_BUTTON;
	} else if ((x >= RIGHT_BUTTON_RECT_START_X) &&
			   (y >= RIGHT_BUTTON_RECT_START_Y) &&
			   (x <= RIGHT_BUTTON_RECT_END_X) &&
			   (y <= RIGHT_BUTTON_RECT_END_Y)) {
		position = TP_RIGHT_BUTTON;
	} else {
		position = TP_TOUCH;
	}

	return position;
}

static bool isFingerEnabledInArea(int area) {
	int i;
	bool ret = false;

	for (i = 0; i < MT_NUMBER_OF_SLOTS; i++) {
		if (gFingers[i].isEnabled && gFingers[i].initPosArea == area) {
			ret = true;
			break;
		}
	}

	return ret;
}

static void transitFingerAction(void) {
	if (gWorkingFinger->mt_tracking_id != -1) {
		if (gWorkingFinger->action == FINGER_OFF) {
			gWorkingFinger->action = FINGER_CONTACT;
		}
	} else {
		/* stop the timer every finger-off */
		stopTouchHoldTimer(&(gWorkingFinger->holdTimer));

		/* If the finger contact position is initiated and soon finger is off the screen,
		   we see it a click.
		*/
		if ((gWorkingFinger->action == FINGER_CONTACT_POSITION_INITIATED) && (gWorkingFinger->isHold == false)) {
			if ((gWorkingFinger->mt_position_x >= RIGHT_BUTTON_RECT_START_X) &&
				(gWorkingFinger->mt_position_y >= RIGHT_BUTTON_RECT_START_Y) &&
				(gWorkingFinger->mt_position_x <= RIGHT_BUTTON_RECT_END_X) &&
				(gWorkingFinger->mt_position_y <= RIGHT_BUTTON_RECT_END_Y)) {
				sendClickReport(0x02);
			} else {
				sendClickReport(0x01);
			}
		}
		
		if (gWorkingFinger->isHold) {
			gWorkingFinger->isHold = false;
			sendFingerOffReport();
		}

		/* clean up finger states */
		gWorkingFinger->mt_position_x = -1;
		gWorkingFinger->mt_position_y = -1;
		gWorkingFinger->action = FINGER_OFF;
		gWorkingFinger->isHold = false;
		gWorkingFinger->isEnabled = false;
	}

	if ((gWorkingFinger->action == FINGER_CONTACT) &&
		(gWorkingFinger->mt_position_x != -1) && 
		(gWorkingFinger->mt_position_y != -1)) {
		gWorkingFinger->action = FINGER_CONTACT_POSITION_INITIATED;
		gWorkingFinger->initPosArea = decideInitPosArea(gWorkingFinger->mt_position_x, gWorkingFinger->mt_position_y);
		gWorkingFinger->isEnabled = !isFingerEnabledInArea(gWorkingFinger->initPosArea);

		startTouchHoldTimer(&(gWorkingFinger->holdTimer), 1, gWorkingFinger);
	}
}

int main(int argc, char *argv[]) {
	struct input_event events[64];
	int fd;
	char *device = "/dev/input/event1";
	char device_name[256] = "Unknown";
	unsigned int bytes_read;
	unsigned int events_read;
	int i;
	bool bExit = false;
	bool isHandled;

	/* init finger state */
	for (i = 0; i < MT_NUMBER_OF_SLOTS; i++) {
		gFingers[i].mt_position_x = -1;
		gFingers[i].mt_position_y = -1;
		gFingers[i].mt_tracking_id = -1;

		gFingers[i].action = FINGER_OFF;
		gFingers[i].initPosArea = TP_TOUCH;
		gFingers[i].isHold = false;
		gFingers[i].isEnabled = false;
	}

	if ((fd = open(device, O_RDONLY)) == -1) {
		printf("Cannot open device %s\n", device);
		goto ERR_INPUT_EVENT_DEVICE;
	}

	if ((hidg_mouse = open(hidg_mouse_device, O_RDWR, 0666)) == -1) {
		printf("Cannot open device %s\n", hidg_mouse_device);
		goto ERR_HIDG_MOUSE_DEVICE;
	}

	vibrator = fopen(vibrator_file, "w");
	if (vibrator) {
		fcntl(fileno(vibrator), F_SETFD, FD_CLOEXEC);
	}	
	
	ioctl(fd, EVIOCGNAME(sizeof(device_name)), device_name);
	printf("Events from %s (%s)\n", device, device_name);
	
	while (1) {
		bytes_read = read(fd, events, sizeof(events));
		if (bytes_read < sizeof(events[0])) {
			break;
		}
		events_read = bytes_read / sizeof(events[0]);
		/* verbose print of touch events */
		/*
		printf("%d evetns read:\n", events_read);
		printf("[no.]\t[type]\t\t[code]\t\t[value]\n");
		printf("--------------------------------------------------------\n");
		for (i = 0; i < events_read; i++) {
			printf("[%d]\t[%d(0x%x)]\t[%d(0x%x)]\t[%d(0x%x)]\n", i, 
				events[i].type, events[i].type, 
				events[i].code, events[i].code, 
				events[i].value, events[i].value);
		}
		printf("\n");
		*/

		for (i = 0; i < events_read; i++) {
			isHandled= true;
			switch (events[i].type) {
				case EV_KEY:
					switch (events[i].code) {
						case BTN_TOUCH:
							//isHandled = false;
							break;
						case BTN_TOOL_FINGER:
							//isHandled = false;
							break;
						default:
							isHandled= false;
							break;
					}
					break;
				case EV_ABS:
					switch (events[i].code) {
						case ABS_MT_TRACKING_ID:
							gWorkingFinger->mt_tracking_id = events[i].value;
							transitFingerAction();
							break;
						case ABS_MT_POSITION_X:
							if (FINGER_MOVE == gWorkingFinger->action) {
								sendMoveReport(events[i].value - gWorkingFinger->mt_position_x, 0);
								gWorkingFinger->mt_position_x = events[i].value;
							} else if (FINGER_CONTACT_POSITION_INITIATED == gWorkingFinger->action) {
								sendMoveReport(events[i].value - gWorkingFinger->mt_position_x, 0);
								gWorkingFinger->mt_position_x = events[i].value;
								gWorkingFinger->action = FINGER_MOVE;
							} else if (FINGER_CONTACT == gWorkingFinger->action) {
								gWorkingFinger->mt_position_x = events[i].value;
								transitFingerAction();
							} else {
								/* should not reach here since no contact is made */
								printf("[Error] Get position x without contact\n");
							}
							break;
						case ABS_MT_POSITION_Y:
							if (FINGER_MOVE == gWorkingFinger->action) {
								sendMoveReport(0, events[i].value - gWorkingFinger->mt_position_y);
								gWorkingFinger->mt_position_y = events[i].value;
							} else if (FINGER_CONTACT_POSITION_INITIATED == gWorkingFinger->action) {
								sendMoveReport(0, events[i].value - gWorkingFinger->mt_position_y);
								gWorkingFinger->mt_position_y = events[i].value;
								gWorkingFinger->action = FINGER_MOVE;
							} else if (FINGER_CONTACT == gWorkingFinger->action) {
								gWorkingFinger->mt_position_y = events[i].value;
								transitFingerAction();
							} else {
								/* should not reach here since no contact is made */
								printf("[Error] Get position y without contact\n");
							}
							break;
						case ABS_MT_SLOT:
							gWorkingFinger = &gFingers[events[i].value];
							break;
						case ABS_MT_TOUCH_MAJOR:
						case ABS_MT_TOUCH_MINOR:							
							//isHandled = false;
							break;
						default:
							isHandled = false;
							break;
					}
					break;
				case EV_SYN:
					break;
				default:
					isHandled = false;
					break;
			}


			//printf("%cCenter (X, Y) ellipse position: (%03d, %04d)", 0x0d, gWorkingFinger->mt_position_x, gWorkingFinger->mt_position_y);
			if (!isHandled) {
				printf("Not handled input event type 0x%x, code 0x%x, value 0x%x\n",
					events[i].type, events[i].code, events[i].value);
			}

			/* finger is in exit area */
			if ((gWorkingFinger->mt_position_x >= EXIT_MOUSE_FUNCTION_RECT_START_X) &&
				(gWorkingFinger->mt_position_y >= EXIT_MOUSE_FUNCTION_RECT_START_Y) &&
				(gWorkingFinger->mt_position_x <= EXIT_MOUSE_FUNCTION_RECT_END_X) &&
				(gWorkingFinger->mt_position_y <= EXIT_MOUSE_FUNCTION_RECT_END_Y)) {
				//printf("\n");
				bExit = true;
				break;
			}			
		}	

		if (bExit) {
			break;
		}		
	}	

	if (vibrator) {
		fclose(vibrator);
	}	
	close(fd);
	close(hidg_mouse);
	
	return 0;


ERR_INPUT_EVENT_DEVICE:
	return 1;


ERR_HIDG_MOUSE_DEVICE:
	close(fd);
	return 2;



	
}
	
	
	



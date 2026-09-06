# 3.3 Board Support Package Files
A board support package (BSP) is a set of standard interfaces implemented for a specific hardware platform, to allow higher-level
code to access the hardware resources. SonicLib defines a set of BSP functions to allow such generic access to the peripheral devices
and other resources on the board. The chirp_bsp.h file contains the definitions of these hardware interfaces. All BSP functions are
specified to use names beginning with the chbsp_ prefix.
The BSP implementation is NOT part of SonicLib. On the contrary, the BSP contains control functions that SonicLib needs but
cannot implement because they are hardware specific.
For example, SonicLib requires an accurate delay function with millisecond granularity, but such a routine can only be implemented
with knowledge of the available hardware timers, etc. So, the SonicLib BSP interface defines the chbsp_delay_ms() routine, which
SonicLib will call when needed. The BSP must therefore provide a routine called chbsp_delay_ms() that is implemented for the
particular board and micro-controller being used.
Other defined functions similarly control pin levels, interrupts, etc. on the board. In general, these BSP routines may be
implemented by “translating” the required operations into calls to the micro-controller vendor’s I/O library functions.
The BSP must be provided separately by the application developer, board vendor, or InvenSense. Contact InvenSense for more
information on available BSPs.

## chirp_bsp.h
The chirp_bsp.h file defines the I/O interfaces that allow the standard SonicLib sensor driver functions to manage one or more
sensors on a specific hardware platform. These include functions to initialize and control the various I/O pins connecting the sensor
to the host system, the SPI or I2C communications interface, interrupt handlers, timer functions, etc.
The BSP developer is responsible for implementing these support functions for the desired platform. Typically, this requires writing
short routines with the specified chbsp_ names and parameters that perform the necessary operations by calling lower-level
functions in a hardware abstraction library (HAL) provided by the host MCU vendor. The BSP routines are basically a translation
layer between the SonicLib callouts and the MCU library functions.
Some BSP functions are optional, depending on the specific runtime requirements (e.g., is non-blocking I/O required?) or
development needs (e.g., is debugging support needed?). See the comments in chirp_bsp.h or the HTML documentation for more
information on implementing the BSP functions and when they are required.
The chirp_bsp.h file should not be modified.

## chirp_board_config.h
The board support package must supply a header file called chirp_board_config.h containing definitions of two symbols used in the
SonicLib driver functions.
The following symbols must be defined in chirp_board_config.h. They are used to determine the size of arrays within the ch_dev_t
device descriptor structure.
• CHIRP_MAX_NUM_SENSORS = maximum number of InvenSense ultrasonic sensors
• CHIRP_NUM_BUSES = number of SPI or I2C bus interfaces that may have ultrasonic sensors attached
• CHIRP_SENSOR_INT_PIN = which sensor interrupt line (0 or 1) is used for interrupts (ICU sensors only)
• CHIRP_SENSOR_TRIG_PIN = which sensor interrupt line (0 or 1) is used for triggering (ICU sensors only)
In addition, the following symbols may be defined in the chirp_board_config.h file to indicate special operating conditions for CHx01
sensors only:
• CHIRP_I2C_SPEED_HZ = I2C bus speed, if used for substitute RTC calibration
• USE_STD_I2C_FOR_IQ = use regular I2C access for I/Q data instead of debug interface
The chirp_board_config.h file must be in the C pre-processor include path when you build your application with SonicLib.

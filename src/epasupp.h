#ifndef _EPASUPP_H_
#define _EPASUPP_H_ 1

#include <wiringPi.h>

/* Number of rows and columns */
#define EPWIDTH  176
#define EPHEIGHT 264
/* size of the bitmap */
#define EPDBYTES ((EPWIDTH/8)*EPHEIGHT)
/* Actual coordinates */
#define X_MAX (EPHEIGHT-1)
#define Y_MAX (EPWIDTH-1)
#define Y_BYTES (EPWIDTH/8)

/* the used pins */
#define RST_PIN 0
#define DC_PIN  6
#define CS_PIN  10
#define BSY_PIN 5
#define KEY1    21
#define KEY2    22
#define KEY3    23
/* DO NOT USE WITH HIFIBERRY DAC+!
  #define KEY4    24 */

/* the commands */
#define PANEL_SETTING                               0x00
#define POWER_SETTING                               0x01
#define POWER_OFF                                   0x02
#define POWER_OFF_SEQUENCE_SETTING                  0x03
#define POWER_ON                                    0x04
#define POWER_ON_MEASURE                            0x05
#define BOOSTER_SOFT_START                          0x06
#define DEEP_SLEEP                                  0x07
#define DATA_START_TRANSMISSION_1                   0x10
#define DATA_STOP                                   0x11
#define DISPLAY_REFRESH                             0x12
#define DATA_START_TRANSMISSION_2                   0x13
#define PARTIAL_DATA_START_TRANSMISSION_1           0x14
#define PARTIAL_DATA_START_TRANSMISSION_2           0x15
#define PARTIAL_DISPLAY_REFRESH                     0x16
#define LUT_FOR_VCOM                                0x20
#define LUT_WHITE_TO_WHITE                          0x21
#define LUT_BLACK_TO_WHITE                          0x22
#define LUT_WHITE_TO_BLACK                          0x23
#define LUT_BLACK_TO_BLACK                          0x24
#define PLL_CONTROL                                 0x30
#define TEMPERATURE_SENSOR_COMMAND                  0x40
#define TEMPERATURE_SENSOR_CALIBRATION              0x41
#define TEMPERATURE_SENSOR_WRITE                    0x42
#define TEMPERATURE_SENSOR_READ                     0x43
#define VCOM_AND_DATA_INTERVAL_SETTING              0x50
#define LOW_POWER_DETECTION                         0x51
#define TCON_SETTING                                0x60
#define TCON_RESOLUTION                             0x61
#define SOURCE_AND_GATE_START_SETTING               0x62
#define GET_STATUS                                  0x71
#define AUTO_MEASURE_VCOM                           0x80
#define VCOM_VALUE                                  0x81
#define VCM_DC_SETTING_REGISTER                     0x82
#define PROGRAM_MODE                                0xA0
#define ACTIVE_PROGRAM                              0xA1
#define READ_OTP_DATA                               0xA2
#define POWER_OPT                                   0xF8

enum epsmap_t {
	epm_none,
	epm_black,
	epm_red,
	epm_both
};
typedef enum epsmap_t epsmap;

enum epsymbols_t {
	ep_null=0,
	ep_box,
	ep_play,
	ep_pause,
	ep_up,
	ep_down,
	ep_prev,
	ep_next,
	ep_fav,
	ep_dnp,
	ep_max
};
typedef enum epsymbols_t epsymbol;

void epsDrawString( epsmap map, unsigned posx, unsigned posy, char *txt, int mag );
void epsDrawSymbol( epsmap map, unsigned x, unsigned y, epsymbol sym );
int  epsDrawChar( epsmap map, unsigned x, unsigned y, int c, int mag );
void epsSetup( void );
void epsDisplay( void );
void epsPartialDisplay( unsigned x, unsigned y, unsigned w, unsigned l );
#define epsClear() epsDisplay( NULL, NULL );
void epsPoweroff( void );
int epsPoweron( void );
void epsSetPixel( epsmap map, unsigned x, unsigned y );
void epsWipe( epsmap map, unsigned x, unsigned y, unsigned w, unsigned l );
void epsWipeFull( epsmap map );
void epsLine( epsmap map, int x0, int y0, int x1, int y1 );
void epsBox( epsmap map, unsigned x0, unsigned y0, unsigned x1, unsigned y1, int filled );
void epsButton( unsigned key, void(*func)(void) );
int epsGetState( void );

#endif

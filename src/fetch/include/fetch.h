/*! \file fetch.h
 * Support implementation of Fetch Language
 * \addtogroup fetch
 * @{
 */


#ifndef FETCH_H_
#define FETCH_H_

#define 		FETCH_MAX_COMMANDS 						8
#define 		FETCH_MAX_DATA_ITEMS					50
#define 		FETCH_MAX_LINE_CHARS					256
#define 		FETCH_MAX_CMD_TOKS					    32
#define 		FETCH_MAX_DATA_TOKS					    256
#define 		FETCH_MAX_CMD_STRLEN					25

#define 		HELP_MAX_DATA_BYTES 					0
#define 		HELP_MAX_DATA_BYTES 					0
#define         HELP_HELPSTRING                         "\r\n"

#define 		GPIO_MAX_DATA_BYTES 					0
#define 		GPIO_HELPSTRING   					    ((const char *) "GPIO:\tgpio:<get,set,clear,configure>:port:pin:<input,output>:<pullup,pulldown,floating,analog>")

#define 		ADC_MAX_DATA_BYTES 					    0
#define 		ADC_HELPSTRING   					    ((const char *) "ADC:\tadc:<conf_adc1,start,stop>:<profile,oneshot,continuous,reset,vref_mv>:<default,demo,PA,PB")

#define 		VERSION_MAX_DATA_BYTES 					0
#define 		VERSION_HELPSTRING   					((const char *) "VERSION:\tversion")

#define 		HEARTBEAT_TOGGLE_MAX_DATA_BYTES 		0
#define 		HEARTBEAT_TOGGLE_HELPSTRING 			((const char *) "HEARTBEAT_TOGGLE:\theartbeat_toggle")

#define 		RESETPINS_MAX_DATA_BYTES 				0
#define 		RESETPINS_HELPSTRING   					((const char *) "RESETPINS:\tresetpins")

#include "fetch_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

int fetch_is_valid_digit(BaseSequentialStream * chp, char * chkdigit);
int fetch_is_valid_EOL(BaseSequentialStream * chp, char * chkEOL);
int fetch_is_valid_whitespace(BaseSequentialStream * chp, char * chkwhitespace);

void fetch_init(BaseSequentialStream *  chp) ;

bool fetch_parse(BaseSequentialStream* chp, char * inputline);
bool fetch_dispatch(BaseSequentialStream* chp, char * command_list[], char * data_list[]);

#ifdef __cplusplus
}
#endif

#endif
/*! @} */

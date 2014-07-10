/*! \file fetch.c
 * Fetch: A DSL for Marionette interaction.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "hal.h"
#include "chprintf.h"

#include "util_general.h"
#include "util_strings.h"
#include "util_messages.h"
#include "gpio.h"

#include "fetch.h"

/************************************************************
BNF Outline for the Fetch Language Grammar

(N,∑,P,S)

N - Non-terminals: { <statement>, <command>, <datastr>, <byte>, <constant> }
∑ - Terminals:     { <command>, <gpio_subcommandA>, <gpio_direction>, <gpio_sense>, <port_subcommand>, <pin_subcommand>, <subcommandD>, <digit>, <EOL>, <whitespace>}
P - Production Rules:

<statement>        ::= <command> <EOL>
                     | <command> ":" <gpio_subcommandA> ":" <port_subcommand> ":" <pin_subcommand> <EOL>
                     | <command> ":" <gpio_subcommandA> ":" <port_subcommand> ":" <pin_subcommand> ":" <gpio_direction> ":" <gpio_sense> <EOL>
                     | <command> ":" <adc_subcommandA> ":" <subcommandB> ":" <subcommandC> ":" <subcommandD> <datastr> <EOL>
<command>          ::= "?"      | "help"     | "gpio"  | "adc"   | "spi"   | "adc" | "resetpins"
<adc_subcommandA>  ::= TBD
<spi_subcommandA>  ::= TBD
<i2c_subcommandA>  ::= TBD
<gpio_subcommandA> ::= "get"    | "set"      | "clear"    | "configure"
<gpio_direction>   ::= "input"  | "output"
<gpio_sense>       ::= "pullup" | "pulldown" | "floating" | "analog"
<port_subcommand>  ::= "porta"  | "portb"    | "portc" | "portd" | "porte" | "portf" | "portg" | "porth" | "porti"
<pin_subcommand>   ::= "pin0"   | "pin1"     | "pin2"  | "pin3"  | "pin4"  | "pin5"  | "pin6"  | "pin7"
                   | "pin8"   | "pin9"     | "pin10" | "pin11" | "pin12" | "pin13" | "pin14" | "pin15"
<subcommandD>      ::= TBD
<datastr>          ::= "(" <byte> ")"
<byte>             ::= <constant>
                     | <constant> <whitespace> <byte>
<constant>         ::= <digit><digit>
<digit>            ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" | "a" | "b" | "c" | "d" | "e"
<EOL>              ::= "\n"
<whitespace>       ::= " " | "\t"

S - Start Symbol: { <statement> }

Comments:
A: This grammar is used to implement an imperitive language called 'Fetch'

B: This is a 'Right Regular Grammar'

Example:
        command:subcommand1:subcommand2:subcommand3:...:subcommandn(datastr1 datastr2 datastr3 ... datastrN)\n
        gpio:set:portd:pin7\n
        gpio:configure:portd:pin7:input:floating\n

***************************************************/

static Command_dictionary          help_lookup      = { .enabled = true, .max_data_bytes = 0, .helpstring = HELP_HELPSTRING};
static Command_dictionary          gpio_lookup      = { .enabled = true, .max_data_bytes = 0, .helpstring = GPIO_HELPSTRING};
static Command_dictionary          version_lookup   = { .enabled = true, .max_data_bytes = 0, .helpstring = VERSION_HELPSTRING};
static Command_dictionary          resetpins_lookup = { .enabled = true, .max_data_bytes = 0, .helpstring = RESETPINS_HELPSTRING};

/*!
 * For parsing we need some large arrays to chop up strings
 * Current solution is allocate these in the .data section
 * not on the stack in \sa fetch_parse
 */
static char     localinput[FETCH_MAX_LINE_CHARS];
static char     commandstr[FETCH_MAX_LINE_CHARS];
static char     datastr[FETCH_MAX_LINE_CHARS];
char      *     command_toks[FETCH_MAX_LINE_CHARS];
char      *     data_toks[FETCH_MAX_LINE_CHARS];


static Command_terminals fetch_terminals =
{
	.command          = {"?", "help", "gpio", "adc", "spi", "i2c", "resetpins"},

	.gpio_subcommandA = {"get", "set", "clear", "configure"},
	.gpio_direction   = {"input", "output"},
	.gpio_sense       = {"pullup", "pulldown", "floating", "analog"},
	.port_subcommand  = {"porta", "portb", "portc", "portd", "porte", "portf", "portg", "porth", "porti" },
	.pin_subcommand   = {"pin0", "pin1", "pin2", "pin3", "pin4", "pin5", "pin6", "pin7", "pin8", "pin9", "pin10", "pin11", "pin12", "pin13", "pin14", "pin15" },
	.subcommandD      = {},
	.digit            = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e"},
	.EOL              = {"\n"},
	.whitespace       = {" ", "\t"}
};


static bool (*cmd_fns[NELEMS(fetch_terminals.command)]) (BaseSequentialStream * chp,
                char * l1[], char * l2[]);

static bool fetch_not_yet(BaseSequentialStream  * chp, char * cmd_list[] UNUSED,
                          char * data_list[] UNUSED)
{
	DBG_MSG(chp, "Not implemented");
	return false;
};

/*!
 * *_is_valid_command...
 * return index to command match in array
 * return -1 on fail to match
 */

/*!
 * Here is the theory on why using static inline here might be useful.
 *
 * https://gcc.gnu.org/onlinedocs/gcc/Inline.html
 *
 *          When a function is both inline and static, if all calls to the function are
 *       integrated into the caller, and the function's address is never used, then the
 *       function's own assembler code is never referenced. In this case, GCC does not
 *       actually output assembler code for the function, unless you specify the option
 *       -fkeep-inline-functions. Some calls cannot be integrated for various reasons
 *       (in particular, calls that precede the function's definition cannot be
 *       integrated, and neither can recursive calls within the definition). If there is
 *       a nonintegrated call, then the function is compiled to assembler code as usual.
 *       The function must also be compiled as usual if the program refers to its
 *       address, because that can't be inlined.
 */

static inline int fetch_is_valid_command(BaseSequentialStream * chp, char * chkcommand)
{
	return(fetch_token_match(chp, fetch_terminals.command, chkcommand,
	                         ((int) NELEMS(fetch_terminals.command)) ));
}

static inline int fetch_is_valid_gpio_direction(BaseSequentialStream * chp,
                char * chkgpio_direction)
{
	return( fetch_token_match(chp, fetch_terminals.gpio_direction, chkgpio_direction,
	                          ((int) NELEMS(fetch_terminals.gpio_direction))) );
}

static inline int fetch_is_valid_gpio_sense(BaseSequentialStream * chp, char * chkgpio_sense)
{
	return(fetch_token_match(chp, fetch_terminals.gpio_sense, chkgpio_sense,
	                         ((int) NELEMS(fetch_terminals.gpio_sense))) );
}

static inline int fetch_is_valid_gpio_subcommandA(BaseSequentialStream * chp,
                char * chkgpio_subcommandA)
{
	return(fetch_token_match(chp, fetch_terminals.gpio_subcommandA, chkgpio_subcommandA,
	                         ((int) NELEMS(fetch_terminals.gpio_subcommandA)) ) );
}

static inline int fetch_is_valid_port_subcommand(BaseSequentialStream * chp,
                char * chkport_subcommand)
{
	return(fetch_token_match(chp, fetch_terminals.port_subcommand, chkport_subcommand,
	                         ((int) NELEMS(fetch_terminals.port_subcommand))) );
}

static inline int fetch_is_valid_pin_subcommand(BaseSequentialStream * chp,
                char * chkpin_subcommand)
{
	return(fetch_token_match(chp, fetch_terminals.pin_subcommand, chkpin_subcommand,
	                         ((int) NELEMS(fetch_terminals.pin_subcommand))) );
}

static inline int fetch_is_valid_digit(BaseSequentialStream * chp, char * chkdigit)
{
	return(fetch_token_match(chp, fetch_terminals.digit, chkdigit,
	                         ((int) NELEMS(fetch_terminals.digit))) );
}

static inline int fetch_is_valid_EOL(BaseSequentialStream * chp, char * chkEOL)
{
	return(fetch_token_match(chp, fetch_terminals.EOL, chkEOL,
	                         ((int) NELEMS(fetch_terminals.EOL))) );
}

static inline int fetch_is_valid_whitespace(BaseSequentialStream * chp, char * chkwhitespace)
{
	return(fetch_token_match(chp, fetch_terminals.whitespace, chkwhitespace,
	                         ((int) NELEMS(fetch_terminals.whitespace))) );
}

/* --- */

/* Help command implementation for fetch language */
static bool fetch_info(BaseSequentialStream * chp, char * cl[] UNUSED, char * dl[] UNUSED)
{
	util_infomsg(chp, "Fetch commandstr Help");
	chprintf(chp, "%s\r\n", version_lookup.helpstring);
	chprintf(chp, "%s\r\n", resetpins_lookup.helpstring);
	chprintf(chp, "%s\r\n", gpio_lookup.helpstring);
	return true;
}

static bool fetch_gpio(BaseSequentialStream  * chp, char * cmd_list[], char * data_list[])
{
	//gpio_dispatch(chp, cmd_list, data_list, CMD_terminals * fetch_cmds);
	if(fetch_is_valid_gpio_subcommandA(chp, cmd_list[ACTION]) >= 0)
	{
		if(fetch_is_valid_port_subcommand(chp, cmd_list[PORT]) >= 0)
		{
			if(fetch_is_valid_pin_subcommand(chp, cmd_list[PIN]) >= 0)
			{
				if (strncasecmp(cmd_list[1], "get", strlen("get") ) == 0)
				{
					chprintf(chp, "%d\r\n", gpio_get(chp, cmd_list));
				}
				else if (strncasecmp(cmd_list[1], "set", strlen("set") ) == 0)
				{
					gpio_set(chp, cmd_list);
				}
				else if (strncasecmp(cmd_list[1], "clear", strlen("clear") ) == 0)
				{
					gpio_clear(chp, cmd_list);
				}
				else if (strncasecmp(cmd_list[1], "config", strlen("config") ) == 0)
				{
					if( (cmd_list[DIRECTION] != NULL) && (cmd_list[SENSE] != NULL))
					{
						gpio_config(chp, cmd_list);
					}
					else
					{
						return false;
					}
				}
				else
				{
					fetch_not_yet(chp, cmd_list, data_list) ;
				}
				return true;
			}
		}
	}
	return false;
}

static bool fetch_resetpins(BaseSequentialStream * chp, char * cmd_list[] UNUSED,
                            char * data_list[] UNUSED)
{
	DBG_MSG(chp, "Resetting pins");
	palInit(&pal_default_config);
	return true;
};

/*! \brief register callbacks for command functions here
                \sa fetch_init
*/
static void fetch_init_cmd_fns(BaseSequentialStream * chp)
{
	for(int i = 0; i < ((int) NELEMS(fetch_terminals.command)); ++i)
	{
		if (strncasecmp(fetch_terminals.command[i], "?", strlen(fetch_terminals.command[i]) ) == 0)
		{
			cmd_fns[i] = fetch_info;
		}
		else if (strncasecmp(fetch_terminals.command[i], "help",
		                     strlen(fetch_terminals.command[i]) ) == 0)
		{
			cmd_fns[i] = fetch_info;
		}
		else if (strncasecmp(fetch_terminals.command[i], "gpio",
		                     strlen(fetch_terminals.command[i]) ) == 0)
		{
			cmd_fns[i] = fetch_gpio;
		}
		else if (strncasecmp(fetch_terminals.command[i], "resetpins",
		                     strlen(fetch_terminals.command[i]) ) == 0)
		{
			cmd_fns[i] = fetch_resetpins;
		}
		else
		{
			cmd_fns[i] = fetch_not_yet;
		}
	}
}

/*! \brief support validation functions
 *   \warning don't call string functions on NULL pointers
 *  Return and index into the array on match
 *  Return =1 on no match
 */
int fetch_token_match(BaseSequentialStream * chp, const char * tok_array[],
                      char * chk_tok,
                      int num_elems)
{
	size_t maxlen = 1;
	for(int i = 0; i < num_elems; ++i)
	{

		chDbgAssert(((tok_array[i] != NULL)
		             && (chk_tok != NULL)), "fetch_token_match() #1", "NULL pointer");

		maxlen = get_longest_str_length(tok_array[i], chk_tok, FETCH_MAX_CMD_STRLEN);

		if (strncasecmp(tok_array[i], chk_tok, maxlen ) == 0)
		{
			return i;
		}
	}
	return -1;
}

/*! \brief catch all function for initialization */
void fetch_init(BaseSequentialStream * chp)
{
	fetch_init_cmd_fns(chp) ;
}

/*! \brief parse the Fetch Statement

        Create a commands list a data list
        Dispatch the commands \sa fetch_dispatch
*/
bool fetch_parse(BaseSequentialStream * chp, char * inputline)
{
	uint8_t n = 0;
	char * lp, *tokp;

	char * colonpart;
	char * parenpart;

	// don't mess up the string passed from outside this function
	strncpy(localinput, inputline, FETCH_MAX_LINE_CHARS);

	// break up into two strings the colons part and the parens part
	if(localinput[0] != '(' || localinput[0] == ')')
	{
		colonpart = _strtok(localinput, "(", &tokp);
		parenpart = _strtok(NULL, "(", &tokp);
	}
	else
	{
		util_errormsg(chp, "No command-(only data?)");
		return false;
	}

	if(colonpart != NULL)
	{
		strncpy(commandstr, colonpart, strlen(colonpart));
		commandstr[strlen(colonpart)] = '\0';
		fetch_remove_spaces(commandstr);
		if(parenpart != NULL)
		{
			strncpy(datastr, parenpart, strlen(parenpart));
			datastr[strlen(parenpart)]    = '\0';
		}
	}
	else
	{
		// empty line
		return true;
	}

	/* break commandstr into array of strings */
	n   = 0;
	lp  = _strtok(commandstr, ":", &tokp);
	command_toks[n] = lp;
	while ((lp = _strtok(NULL, ":", &tokp)) != NULL)
	{
		if (n >= FETCH_MAX_COMMANDS)
		{
			util_errormsg(chp, "Too many commands.");
			command_toks[0] = NULL;
			break;
		}
		command_toks[++n] = lp;
	}
	command_toks[++n] = "\0";

	if(parenpart != NULL)
	{
		/* break data into array of strings */
		n   = 0;
		lp  = _strtok(datastr, " ", &tokp);
		data_toks[n] = lp;
		while ((lp = _strtok(NULL, " ", &tokp)) != NULL)
		{
			if (n >= FETCH_MAX_COMMANDS)
			{
				util_errormsg(chp, "Too many commands.");
				data_toks[0] = NULL;
				break;
			}
			data_toks[++n] = lp;
		}
		data_toks[++n] = "\0";
	}
	else
	{
		data_toks[0] = NULL;
	}
	return(fetch_dispatch(chp, command_toks, data_toks));
}

/*! \brief dispatch the commands from here
        a list of colon separated command strings is in the command_list
        a list of space separated data is in data_list

        \warning data_list[0] may be null.

        \sa fetch_init_cmd_fns
 */
bool fetch_dispatch(BaseSequentialStream * chp, char * command_list[], char * data_list[])
{
	int cindex = fetch_is_valid_command(chp, command_list[0]);

	if(cindex < 0)
	{
		DBG_MSG(chp, "Unrecognized command.");
		return false;
	}

	return ( (*cmd_fns[cindex]) (chp, command_list, data_list) );
}


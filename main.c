/*---------------------------------------------------------------------------*/
/*                                                                           */
/*        Module:     main.c                                                 */
/*        Author:     James Pearman                                          */
/*        Created:    11 Aug 2012                                            */
/*                                                                           */
/*        Revisions:  V0.1                                                   */
/*                                                                           */
/*---------------------------------------------------------------------------*/
/*                                                                           */
/*        Description:                                                       */
/*                                                                           */
/*        VEX cortex version of the stm32flash download software             */
/*                                                                           */
/*        Major changes as follows:                                          */
/*        Ability to enter the user boot mode by either RTS toggling or      */
/*        C9 commands.                                                       */
/*        Auto baud retry up to 4 times.                                     */
/*        removal of reset code download to RAM, not necessary with the      */
/*        refactoring of this file for easier reading                        */
/*        change of default baudrate to 115200, the only speed at which      */
/*        this will work with the cortex.                                    */
/*        addition of a "quiet" mode to skip debug and progress printing     */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
/*                                                                           */



/*
  stm32flash - Open Source ST STM32 flash program for *nix
  Copyright (C) 2010 Geoffrey McRae <geoff@spacevs.com>
..Copyright (C) 2011 Steve Markgraf <steve@steve-m.de>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "utils.h"
#include "serial.h"
#include "stm32.h"
#include "parser.h"

#include "parsers/binary.h"
#include "parsers/hex.h"

/* device globals */
serial_t        *serial         = NULL;
stm32_t         *stm            = NULL;

void            *p_st           = NULL;
parser_t        *parser         = NULL;

/* settings */
char            *device         = NULL;
serial_baud_t   baudRate        = SERIAL_BAUD_115200;
int             rd              = 0;
int             wr              = 0;
int             wu              = 0;
int             npages          = 0xFF;
char            verify          = 0;
int             retry           = 10;
char            exec_flag       = 0;
uint32_t        execute         = 0;
char            init_flag       = 1;
char            force_binary    = 0;
char            reset_flag      = 1;
char            *filename;

int             vex_user_program = 0;
char            quietmode        = 0;

/* functions */
int     vex_enter_user_program_rts( void );
int     vex_enter_user_program_cmd( void );

int     read_flash( void );
int     write_unprotect_flash( void );
int     write_flash( void );
void    cleanup( void );
parser_err_t    open_parser(void);

int  parse_options(int argc, char *argv[]);
void show_help(char *name);

int main(int argc, char* argv[])
{
        int ret = 1;
        parser_err_t perr;
        
        if (parse_options(argc, argv) != 0)
            return(0);

        if(!quietmode)
            printf("VEX cortex flash loader\n\n");
    

        // Open file parser
        perr = open_parser();

        if( perr != PARSER_ERR_OK )
            return(perr);

        // Open serial device            
        serial = serial_open(device);
        if (!serial)
            {
            perror(device);
            cleanup();
            return(-1);
            }

        // Put cortex into boot load mode
        if(vex_user_program == 2)
            vex_enter_user_program_rts();
        else
        if(vex_user_program != 0)
            vex_enter_user_program_cmd();

        // Setup serial port for bootloader
        if (serial_setup( serial, baudRate, SERIAL_BITS_8, SERIAL_PARITY_EVEN, SERIAL_STOPBIT_1) != SERIAL_ERR_OK)
            {
            perror(device);
            cleanup();
            return(-1);
            }

        // 1/10 sec delay before comms start
        usleep(100000);

        // RTS needs to be low for user program to be reset - no idea why
        // May need to do something with the DTR line for the USB, not sure yet
        //
        serial_set_rts( serial, 0 );

        // 1/10 sec delay before comms start
        usleep(100000);
        
        // Show serial configuration for debug
        if(!quietmode)
            printf("Serial Config: %s\n", serial_get_setup_str(serial));
        
        // Init the STM32 communicationst 
        if (!(stm = stm32_init(serial, init_flag)))
            {
            cleanup();
            return(-1);
            }

        if(!quietmode) {
            // Print some info about the cortex
            printf("Version      : 0x%02x\n", stm->bl_version);
            printf("Option 1     : 0x%02x\n", stm->option1);
            printf("Option 2     : 0x%02x\n", stm->option2);
            printf("Device ID    : 0x%04x (%s)\n", stm->pid, stm->dev->name);
            printf("RAM          : %dKiB  (%db reserved by bootloader)\n", (stm->dev->ram_end - 0x20000000) / 1024, stm->dev->ram_start - 0x20000000);
            printf("Flash        : %dKiB (sector size: %dx%d)\n", (stm->dev->fl_end - stm->dev->fl_start ) / 1024, stm->dev->fl_pps, stm->dev->fl_ps);
            printf("Option RAM   : %db\n", stm->dev->opt_end - stm->dev->opt_start);
            printf("System RAM   : %dKiB\n", (stm->dev->mem_end - stm->dev->mem_start) / 1024);
            }
            
        // Read flash if necessary
        if( rd ) {
            if( read_flash() < 0 )
                {
                cleanup();
                return(-1);
                }
            }    
        else if (wu) {
            write_unprotect_flash();
            }    
        else if (wr) {
            if( write_flash() < 0 )
                {
                cleanup();
                return(-1);
                }
            }   

        // ececute code ?
        if (stm && exec_flag)
            {
            if (execute == 0)
                execute = stm->dev->fl_start;

            if(!quietmode) {
                fprintf(stdout, "\nStarting execution at address 0x%08x... \n", execute);
                fflush(stdout);
                }
            
            if (stm32_go(stm, execute))
                {
                reset_flag = 0;
                if(!quietmode)
                    fprintf(stdout, "done.\n");
                }
            else
                if(!quietmode)
                    fprintf(stdout, "failed.\n");
           }

        // reset device ?
        /*
        if (stm && reset_flag && !exec_flag)
            {
            fprintf(stdout, "\nResetting device... ");
            fflush(stdout);
            if (stm32_reset_device(stm))
                fprintf(stdout, "done.\n");
            else
                fprintf(stdout, "failed.\n");
            }
        */
        
        // deallocate memory etc.
        cleanup();
        
        printf("\n");
        return ret;
}

/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*  Enter user boot by sending Enter bootloader command                        */
/*                                                                             */
/*-----------------------------------------------------------------------------*/

int
vex_enter_user_program_cmd()
{
    char    buf[5] = {0xC9, 0x36, 0xB8, 0x47, 0x25 };

    if(serial)
        {
        // Setup serial port
        if (serial_setup( serial, baudRate, SERIAL_BITS_8, SERIAL_PARITY_NONE, SERIAL_STOPBIT_1) != SERIAL_ERR_OK)
            {
            perror(device);
            cleanup();
            return(-1);
            }

        usleep(10000);
        
        if(!quietmode)
            printf("Send bootloader start command\n");

        serial_write( serial, buf, 5 );
        serial_write( serial, buf, 5 );
        serial_write( serial, buf, 5 );
        serial_write( serial, buf, 5 );
        serial_write( serial, buf, 5 );
    
        usleep(250000);
        }

    return(1);
}


/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*  Enter user boot by pulsing RTS line                                        */
/*                                                                             */
/*-----------------------------------------------------------------------------*/

int
vex_enter_user_program_rts()
{
    char    buf[1];
         
	if(serial)
	    {
    	if (serial_setup( serial, SERIAL_BAUD_9600,	SERIAL_BITS_8, SERIAL_PARITY_NONE, SERIAL_STOPBIT_1	) != SERIAL_ERR_OK)
	    	{
		    perror(device);
		    return(0);
	        }
    
        if(!quietmode)
            printf("Send bootloader start command (RTS)\n");

        // send 1 char as driver has a bug
        buf[0] = 0x00;
        serial_write( serial, buf, 1 );
    
        serial_set_rts( serial, 1 );
        usleep(5000);
    
        serial_set_rts( serial, 0 );
        usleep(15000);

        serial_set_rts( serial, 1 );
        usleep(10000);
        // tx
        buf[0] = 0xF0;
        serial_write( serial, buf, 1 );
    
        usleep(20000);
    
        serial_set_rts( serial, 0 );

        usleep(250000);
        }

    return(1);
}

/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*                                                                             */
/*-----------------------------------------------------------------------------*/

int
read_flash()
{
    parser_err_t perr;
    uint8_t         buffer[256];
    uint32_t        addr;
    unsigned int    len;
    
    if (rd)
        {
        printf("\n");

        if ((perr = parser->open(p_st, filename, 1)) != PARSER_ERR_OK)
            {
            fprintf(stderr, "%s ERROR: %s\n", parser->name, parser_errstr(perr));
        
            if (perr == PARSER_ERR_SYSTEM) perror(filename);
            return(-1);
            }

        addr = stm->dev->fl_start;
        
        while(addr < stm->dev->fl_end)
            {
            uint32_t left   = stm->dev->fl_end - addr;
            len             = sizeof(buffer) > left ? left : sizeof(buffer);
            if (!stm32_read_memory(stm, addr, buffer, len))
                {
                fprintf(stderr, "Failed to read memory at address 0x%08x, target write-protected?\n", addr);
                return(-1);
                }
        
            assert(parser->write(p_st, buffer, len) == PARSER_ERR_OK);
            addr += len;

            if(!quietmode) {
                fprintf(stdout, "Read address 0x%08x (%.2f%%) \r",addr,
                                (100.0f / (float)(stm->dev->fl_end - stm->dev->fl_start)) * (float)(addr - stm->dev->fl_start) );
                fflush(stdout);
                }
            }
            
        if(!quietmode)
            fprintf(stdout, "\nDone.\n");

        return(1);
        }
        
    return(0);
}

/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*                                                                             */
/*-----------------------------------------------------------------------------*/

int
write_unprotect_flash()
{
    if (wu)
        {
        if(!quietmode)
            fprintf(stdout, "Write-unprotecting flash\n");
        
        /* the device automatically performs a reset after the sending the ACK */
        reset_flag = 0;
        
        stm32_wunprot_memory(stm);
        
        if(!quietmode)
            fprintf(stdout, "Done.\n");
        
        return(1);
        }
        
    return(0);
}

/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*                                                                             */
/*-----------------------------------------------------------------------------*/

int
write_flash()
{
    uint8_t         buffer[256];
    uint32_t        addr;
    unsigned int    len;
    int             failed = 0;

    if (wr)
        {
        printf("\n");

        off_t   offset = 0;
        ssize_t r;
        unsigned int size = parser->size(p_st);

        if (size > stm->dev->fl_end - stm->dev->fl_start)
            {
            fprintf(stderr, "File provided larger then available flash space.\n");
            return(-1);
            }

        stm32_erase_memory(stm, npages);

        addr = stm->dev->fl_start;

        while(addr < stm->dev->fl_end && offset < size)
            {
            uint32_t left   = stm->dev->fl_end - addr;
            len             = sizeof(buffer) > left ? left : sizeof(buffer);
            len             = len > size - offset ? size - offset : len;

            if (parser->read(p_st, buffer, &len) != PARSER_ERR_OK)
                return(-1);
        
            failed = 0;
            
            do
                {
                if (!stm32_write_memory(stm, addr, buffer, len))
                    {
                    fprintf(stderr, "\nFailed to write memory at address 0x%08x\n", addr);
                    return(-1);
                    }

                if (verify)
                    {
                    uint8_t compare[len];
                
                    if (!stm32_read_memory(stm, addr, compare, len))
                        {
                        fprintf(stderr, "\nFailed to read memory at address 0x%08x\n", addr);
                        return(-1);
                        }

                    for(r = 0; r < len; ++r)
                        {
                        if (buffer[r] != compare[r])
                            {
                            if (failed == retry)
                                {
                                fprintf(stderr, "\nFailed to verify at address 0x%08x, expected 0x%02x and found 0x%02x\n", (uint32_t)(addr + r), buffer [r], compare[r] );
                                return(-1);
                                }
                            ++failed;
                            }

                        failed = 0;
                        }
                    } 
                }while( failed > 0 );
            
            addr    += len;
            offset  += len;

            if(!quietmode) {
                fprintf(stdout, "Wrote %saddress 0x%08x (%.2f%%) \r", verify ? "and verified " : "", addr, (100.0f / size) * offset );
                fflush(stdout);
                }
            }

        if(!quietmode)
            printf("\n");
            
        return(1);
        }
        
    return(0);
}


/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*                                                                             */
/*-----------------------------------------------------------------------------*/

void
cleanup()
{
    usleep(20000);
    
    if (p_st  )
        parser->close(p_st);
        
    if (stm   )
        stm32_close  (stm);
    
    if (serial)
        serial_close (serial);
}

/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*                                                                             */
/*-----------------------------------------------------------------------------*/

parser_err_t
open_parser()
{
        parser_err_t perr;

        // Are we writing flash ?
        if (wr)
            {
            /* first try hex */
            if (!force_binary)
                {
                parser = &PARSER_HEX;
                p_st = parser->init();
                if (!p_st)
                    {
                    fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
                    return(PARSER_ERR_SYSTEM);
                    }
                }

            if (force_binary || (perr = parser->open(p_st, filename, 0)) != PARSER_ERR_OK)
                {
                if (force_binary || perr == PARSER_ERR_INVALID_FILE)
                    {
                    if (!force_binary)
                        {
                        parser->close(p_st);
                        p_st = NULL;
                        }

                    /* now try binary */
                    parser = &PARSER_BINARY;
                    p_st = parser->init();
                    if (!p_st)
                        {
                        fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
                        
                        return(PARSER_ERR_SYSTEM);
                        }
                    perr = parser->open(p_st, filename, 0);
                    }

                /* if still have an error, fail */
                if (perr != PARSER_ERR_OK)
                    {
                    fprintf(stderr, "%s ERROR: %s\n", parser->name, parser_errstr(perr));
                    
                    if (perr == PARSER_ERR_SYSTEM)
                        perror(filename);
                    
                    return(perr);
                    }
                }

                fprintf(stdout, "Using Parser : %s\n", parser->name);
            }
        else if (rd )
        // reading flash ?
            {
            parser = &PARSER_BINARY;
            p_st = parser->init();
            if (!p_st)
                {
                fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
                return(PARSER_ERR_SYSTEM);
                }
            }
            
    return( PARSER_ERR_OK );
}

/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*                                                                             */
/*-----------------------------------------------------------------------------*/

int parse_options(int argc, char *argv[]) {
        int c;
        while((c = getopt(argc, argv, "b:r:w:e:vn:g:fchuXq12")) != -1) {
                switch(c) {
                        case 'X':
                                if( vex_user_program == 0 )
                                    vex_user_program = 1;
                                break;
                                
                        case '1':
                                vex_user_program = 1;
                                break;
                        case '2':
                                vex_user_program = 2;
                                break;
                                
                        case 'q':
                                quietmode = 1;
                                break;
                                
                        case 'b':
                                baudRate = serial_get_baud(strtoul(optarg, NULL, 0));
                                if (baudRate == SERIAL_BAUD_INVALID) {
                                        fprintf(stderr, "Invalid baud rate, valid options are:\n");
                                        for(baudRate = SERIAL_BAUD_1200; baudRate != SERIAL_BAUD_INVALID; ++baudRate)
                                                fprintf(stderr, " %d\n", serial_get_baud_int(baudRate));
                                        return 1;
                                }
                                break;

                        case 'r':
                        case 'w':
                                rd = rd || c == 'r';
                                wr = wr || c == 'w';
                                if (rd && wr) {
                                        fprintf(stderr, "ERROR: Invalid options, can't read & write at the same time\n");
                                        return 1;
                                }
                                filename = optarg;
                                break;
                        case 'e':
                                npages = strtoul(optarg, NULL, 0);
                                if (npages > 0xFF || npages < 0) {
                                        fprintf(stderr, "ERROR: You need to specify a page count between 0 and 255");
                                        return 1;
                                }
                                break;
                        case 'u':
                                wu = 1;
                                if (rd || wr) {
                                        fprintf(stderr, "ERROR: Invalid options, can't write unprotect and read/write at the same time\n");
                                        return 1;
                                }
                                break;
                        case 'v':
                                verify = 1;
                                break;

                        case 'n':
                                retry = strtoul(optarg, NULL, 0);
                                break;

                        case 'g':
                                exec_flag = 1;
                                execute   = strtoul(optarg, NULL, 0);
                                break;

                        case 'f':
                                force_binary = 1;
                                break;

                        case 'c':
                                init_flag = 0;
                                break;

                        case 'h':
                                show_help(argv[0]);
                                return 1;
                }
        }

        for (c = optind; c < argc; ++c) {
                if (device) {
                        fprintf(stderr, "ERROR: Invalid parameter specified\n");
                        show_help(argv[0]);
                        return 1;
                }
                device = argv[c];
        }

        if (device == NULL) {
                fprintf(stderr, "ERROR: Device not specified\n");
                show_help(argv[0]);
                return 1;
        }

        if (!wr && verify) {
                fprintf(stderr, "ERROR: Invalid usage, -v is only valid when writing\n");
                show_help(argv[0]);
                return 1;
        }

        return 0;
}

/*-----------------------------------------------------------------------------*/
/*                                                                             */
/*                                                                             */
/*-----------------------------------------------------------------------------*/

void show_help(char *name) {
        fprintf(stderr,
#ifdef __WIN32__
                "Usage: %s [-bvngfhc] [-[rw] filename] COM1\n"
#else
                "Usage: %s [-bvngfhc] [-[rw] filename] /dev/tty.usbserial\n"
#endif
                "       -b rate         Baud rate (default 115200)\n"
                "       -X              Enter VEX user program mode\n" 
                "       -X1             Enter VEX user program mode using C9 commands\n" 
                "       -X2             Enter VEX user program mode using old style RTS control\n" 
                "       -r filename     Read flash to file\n"
                "       -w filename     Write flash to file\n"
                "       -u              Disable the flash write-protection\n"
                "       -e n            Only erase n pages before writing the flash\n"
                "       -v              Verify writes\n"
                "       -n count        Retry failed writes up to count times (default 10)\n"
                "       -g address      Start execution at specified address (0 = flash start)\n"
                "       -f              Force binary parser\n"
                "       -h              Show this help\n"
                "       -c              Resume the connection (don't send initial INIT)\n"
                "                       *Baud rate must be kept the same as the first init*\n"
                "                       This is useful if the reset fails\n"
                "\n"
                "Examples:\n"
                "       Get device information:\n"
#ifdef __WIN32__
                "               %s -X COM1\n"
#else
                "               %s -X /dev/tty.usbserial\n"
#endif
                "\n"
                "       Write with verify and then start execution:\n"
#ifdef __WIN32__
                "               %s -X -w filename -v -g 0x0 COM1\n"
#else
                "               %s -X -w filename -v -g 0x0 //dev/tty.usbserial\n"
#endif
                "\n"
                "       Read flash to file:\n"
#ifdef __WIN32__
                "               %s -X -r filename COM1\n",
#else
                "               %s -X -r filename /dev/tty.usbserial\n",
#endif
                name,
                name,
                name,
                name
        );
}


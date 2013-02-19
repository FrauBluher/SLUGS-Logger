/* 
 * File:   Uart2.h
 * Author: Jesse
 *
 * Created on February 19, 2013, 1:56 PM
 */

#ifndef UART2_H
#define	UART2_H

#ifndef UART2_BUFFER_SIZE
#define UART2_BUFFER_SIZE 8
#endif

#define FCY 40000000 // assumes 40Mhz freq
#define BAUDRATE 115200 // assumes 115200 baud rate
#define BRGVAL ((FCY/BAUDRATE)/16)-1 // used in Uart2Init()

void Uart2Init(void); // initializes UART and DMA to a ping-pong buffer

void Uart2PrintChar(char in); // sends a character with UART2

#endif	/* UART2_H */

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define __fp16 _Float16
static inline int32_t __SSAT(int32_t x, uint32_t b){int32_t m=(1<<(b-1))-1; return x>m?m:(x<-m-1?-m-1:x);}
static inline int32_t sat16(int32_t x){return x>32767?32767:(x<-32768?-32768:x);}
static inline uint32_t __QADD16(uint32_t a,uint32_t b){int32_t lo=sat16((int16_t)a+(int16_t)b),hi=sat16((int16_t)(a>>16)+(int16_t)(b>>16));return (uint16_t)lo|((uint32_t)(uint16_t)hi<<16);}
static inline uint32_t __QSUB16(uint32_t a,uint32_t b){int32_t lo=sat16((int16_t)a-(int16_t)b),hi=sat16((int16_t)(a>>16)-(int16_t)(b>>16));return (uint16_t)lo|((uint32_t)(uint16_t)hi<<16);}
static inline uint32_t __SADD16(uint32_t a,uint32_t b){return (uint16_t)((int16_t)a+(int16_t)b)|((uint32_t)(uint16_t)((int16_t)(a>>16)+(int16_t)(b>>16))<<16);}
static inline uint32_t __CLZ(uint32_t x){return x?__builtin_clz(x):32;}
typedef struct { int dummy; } RNG_HandleTypeDef;
static inline uint32_t HAL_RNG_GetRandomNumber(RNG_HandleTypeDef* h){(void)h; static uint32_t s=2463534242u; s^=s<<13; s^=s>>17; s^=s<<5; return s;}
#define __STATIC_INLINE static inline
typedef struct { volatile uint32_t CYCCNT; } DWT_Type;
static DWT_Type dwt_stub; 
#define DWT (&dwt_stub)
static inline uint32_t HAL_RCC_GetHCLKFreq(void){return 216000000u;}
#define __PKHBT(a,b,s) ((((uint32_t)(a)) & 0x0000FFFFu) | ((((uint32_t)(b)) << (s)) & 0xFFFF0000u))
typedef struct { int dummy; } I2S_HandleTypeDef;
typedef struct { int dummy; } SPI_HandleTypeDef;
typedef struct { int dummy; } TIM_HandleTypeDef;
typedef struct { int dummy; } UART_HandleTypeDef;
/* APSR: host can't read the ARM overflow flag; report "no overflow" */
typedef union { struct { uint32_t _r:28; uint32_t V:1; uint32_t C:1; uint32_t Z:1; uint32_t N:1; } b; uint32_t w; } APSR_Type;
static inline uint32_t __get_APSR(void){return 0;}
typedef struct { volatile uint32_t IDR, ODR, BSRR; } GPIO_TypeDef;
extern GPIO_TypeDef host_gpio[5];
#define GPIOA (&host_gpio[0])
#define GPIOB (&host_gpio[1])
#define GPIOC (&host_gpio[2])
#define GPIOD (&host_gpio[3])
#define GPIOE (&host_gpio[4])
#define GPIO_PIN_0 0x1u
#define GPIO_PIN_1 0x2u
#define GPIO_PIN_2 0x4u
#define GPIO_PIN_3 0x8u
#define GPIO_PIN_4 0x10u
#define GPIO_PIN_5 0x20u
#define GPIO_PIN_6 0x40u
#define GPIO_PIN_7 0x80u
#define GPIO_PIN_8 0x100u
#define GPIO_PIN_9 0x200u
#define GPIO_PIN_10 0x400u
#define GPIO_PIN_11 0x800u
#define GPIO_PIN_12 0x1000u
#define GPIO_PIN_13 0x2000u
#define GPIO_PIN_14 0x4000u
#define GPIO_PIN_15 0x8000u

#ifndef _DT_BINDINGS_STM32L476_PINFUNC_H
#define _DT_BINDINGS_STM32L476_PINFUNC_H

#define PA(_id)	 ((_id) * 0x100)
#define PB(_id)	 (((_id) * 0x100) + 0x1000)
#define PC(_id)	 (((_id) * 0x100) + 0x2000)
#define PD(_id)	 (((_id) * 0x100) + 0x3000)
#define PE(_id)	 (((_id) * 0x100) + 0x4000)
#define PF(_id)	 (((_id) * 0x100) + 0x5000)
#define PG(_id)	 (((_id) * 0x100) + 0x6000)
#define PH(_id)	 (((_id) * 0x100) + 0x7000)

#define GPIO	0
#define AF0	1
#define AF1	2
#define AF2	3
#define AF3	4
#define AF4	5
#define AF5	6
#define AF6	7
#define AF7	8
#define AF8	9
#define AF9	10
#define AF10	11
#define AF11	12
#define AF12	13
#define AF13	14
#define AF14	15
#define AF15	16

#define STM32L476_FUNC(_pin, _func) \
	((_pin) + _func)

#endif /* _DT_BINDINGS_STM32L476_PINFUNC_H */

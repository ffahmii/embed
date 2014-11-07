// Pada tugas ini, multitasking dilakukan dengan metode
// non Nonpreemptive multitasking, atau cooperative thread,
// http://en.wikipedia.org/wiki/Nonpreemptive_multitasking

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define enter_critical_section()    cli()       // disable interrupt
#define leave_critical_section()    sei()       // enable interrupt

static int16_t time;

// BEGIN task defenition, lihat tasknya di tasks.c
extern struct {
	int16_t counting;
	int16_t (*function)();
} all_task[];
extern int task_count();
extern void task_setup();
// END task defenition

// interrupt ini dijalankan setiap 1 ms
ISR(TIMER0_COMP_vect) {
	time++;
	// gantian 187 dan 188 agar akurasi mendekati 187,5
	if(OCR0 == 187) OCR0 = 188;
	else            OCR0 = 187;

	for(int i = 0; i < task_count(); ++i) {
		// kurangi hanya jika tidak overflow
		if(all_task[i].counting != INT16_MIN && all_task[i].counting != INT16_MAX) {
			all_task[i].counting--;
		}
	}
}

int main() {
	// setup interrupt dari counter0 dengan mode CTC
	// karna kecepatan CPU adalah 12Mhz maka untuk setiap 1ms butuh 12000 clock
	// kita set prescaler jadi 64 maka TOP untuk CTC adalah 12000/64 = 187.5
	// http://www.atmel.com/Images/Atmel-8155-8-bit-Microcontroller-AVR-ATmega32A_Datasheet.pdf,
	// halaman 68-81
	OCR0 = 187;
	TCNT0 = 0;
	TCCR0 |= _BV(WGM01) | _BV(CS01) | _BV(CS00);
	TIMSK |= _BV(OCIE0);
	sei();

	task_setup();

	while(1) {
		int16_t tmp[task_count()];

		// dapatkan counting dari setiap task, harus dalam critical_section,
		// agar setiap task tetap sinkron
		// http://en.wikipedia.org/wiki/Concurrency_(computer_science)
		enter_critical_section(); {
			for(int i = 0; i < task_count(); ++i) tmp[i] = all_task[i].counting;
		} leave_critical_section();


		// jalankan setiap task
		for(int i = 0; i < task_count(); ++i) {
			if(tmp[i] <= 0) {
				tmp[i] = all_task[i].function();
				if(tmp[i] < 0) tmp[i] = 0;
			} else {
				tmp[i] = 0;
			}
		}

		// update counting dari setiap task, harus dalam critical_section,
		// agar setiap task tetap sinkron
		// http://en.wikipedia.org/wiki/Concurrency_(computer_science)
		enter_critical_section(); {
			for(int i = 0; i < task_count(); ++i) {
				if(tmp[i] == INT16_MAX) {
					all_task[i].counting = INT16_MAX;
				}
				else {
					all_task[i].counting += tmp[i];
				}
			}
		} leave_critical_section();
	}
	
}

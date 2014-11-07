// Pada tugas ini, multitasking dilakukan dengan metode
// non Nonpreemptive multitasking, atau cooperative thread,
// http://en.wikipedia.org/wiki/Nonpreemptive_multitasking

#include <avr/io.h>
#include <util/delay.h>
#include <avr/wdt.h>
#include "lcd.h"
#include "i2c.h"

#define LED_DDR              DDRA
#define LED_PORT             PORTA

#define BTN_DDR              DDRB
#define BTN_PORT             PORTB
#define BTN_PIN              PINB
#define BTN_FAST             0
#define BTN_SLOW			 1

#define ALAMAT_SENSOR		 0xEE
#define MAX_RANGE_CM         150
#define MAX_RANGE_REGISTER   ((((MAX_RANGE_CM * 10) - 85) / 43) + 1)

int16_t last_value = -1;
int16_t password[4] = {{-1},{-1},{-1},{-1}};		// tampungan password awal
int16_t password_harusnya[4] = {{1},{1},{1},{1}};	// password sebenarnya
int16_t passwordIterator = 0;						// sebagai flag untuk menandakan pointer pada pengisian password
int16_t timeoutIterator = 0;						// sebagai flag untuk mempercepat kecepatan LED, jika waktu mendekati finish
int16_t newPasswordIterator = 0;					// sebagai flag untuk menandakan pointer pada pengisian ganti password
int16_t DDRAM_AWAL_PASS = 0x40;
int16_t DDRAM_AWAL_NEW_PASS = 0x40;
int16_t inChPass = 0;
int16_t led_stop = 0;
int16_t timeout_stop = 0;

// list dari task yang akan digunakan pada main program
static int16_t led_task();
static int16_t tombol_task();
static int16_t welcome_task();
static int16_t inputpassword_task();
static int16_t password_task();
static int16_t sensor_task();
static int16_t timeout_task();
static int16_t chpass_task();

static int16_t led_delay;							// delay dari LED
static uint8_t fast_btn;							// button 0
static uint8_t slow_btn;							// button 1

// struktur dari list task
struct {
	int16_t counting;
	int16_t (*function)();
} all_task[] = {
	{ 0, led_task },
	{ 0, welcome_task },
	{ 1000, inputpassword_task },
	{ 2000, password_task },
	{ 2000, sensor_task },
	{ 2000, tombol_task },
	{ 2000, timeout_task },
	{ INT16_MAX, chpass_task }
};

// fungsi untuk mengembalikan jumlah dari task
int task_count() {
	return (sizeof(all_task) / sizeof(all_task[0]));
}

// fungsi untuk inisialisasi task
void task_setup() {
	LED_DDR = 0xff;
	LED_PORT = 0xfe;

	BTN_DDR &= ~(_BV(BTN_SLOW) | _BV(BTN_FAST));
	BTN_PORT |= (_BV(BTN_SLOW) | _BV(BTN_FAST));
	
	lcd_init();
	stdout = lcd_file_out();
	
	i2c_init();
	i2c_transmit(ALAMAT_SENSOR, 2, MAX_RANGE_REGISTER);

	led_delay = 300;
	fast_btn = slow_btn = 0;
}

// fungsi tambahan untuk mengecek password
int check_password(int16_t a[], int16_t b[]){
	int temp = 1;
	for (int i = 0; i < 4; i++){
		if (a[i] != b[i]) {
			temp = 0;
			break;
		}
	}
	return temp;
}

// fungsi tambahan untuk mereset program
void reset() {
	timeout_stop = 0;
	led_stop = 0;
	inChPass = 0;
	newPasswordIterator = 0;
	DDRAM_AWAL_NEW_PASS = 0x40;
	led_delay = 300;
	fast_btn = 0;
	timeoutIterator = 0;
	passwordIterator = 0;
	last_value = -1;
	password[0] = -1;
	password[1] = -1;
	password[2] = -1;
	password[3] = -1;
	all_task[0].counting = led_delay;
	all_task[3].counting = 2000;
	all_task[4].counting = 2000;
	all_task[5].counting = 2000;
	all_task[6].counting = 2000;
	DDRAM_AWAL_PASS = 0x40;
}

// fungsi tambahan untuk mempersiapkan masuk
// ke interface change password
void prepareChangePassword() {
	all_task[7].counting = 0;
}

// task untuk mengatur delay dan pergerakan
// dari lampu LED
static int16_t led_task() {
	static uint8_t is_left = 1;
	
	if(!led_stop) {
		if(is_left) {
			// shift left
			LED_PORT = (LED_PORT << 1) | _BV(0);
			if(LED_PORT == 0x7f) is_left = 0;
		}
		else {
			// shift right
			LED_PORT = (LED_PORT >> 1) | _BV(7);
			if(LED_PORT == 0xfe) is_left = 1;
		}
		// delay sesuai dengan speed sekarang
		return led_delay;
	}
	else {
		return INT16_MAX;
	}
}

// task untuk print di LCD berupa
// SELAMAT
// DATANG
static int16_t welcome_task() {
	lcd_clear();
	lcd_set_ddram_addr(0x00);
	printf("SELAMAT");
	lcd_set_ddram_addr(0x40);
	printf("DATANG");
	
	return INT16_MAX;
}

// task untuk print di LCD berupa
// MASUKKAN
// PASSWORD
static int16_t inputpassword_task() {
	lcd_clear();
	lcd_set_ddram_addr(0x00);
	printf("MASUKKAN");
	lcd_set_ddram_addr(0x40);
	printf("PASSWORD");
	
	return INT16_MAX;
}

// task untuk print di LCD berupa
// PASSWORD
//   ____
static int16_t password_task() {
	lcd_clear();
	lcd_set_ddram_addr(0x00);
	printf("PASSWORD");
	lcd_set_ddram_addr(0x42);
	printf("____");
	
	return INT16_MAX;
}

// task untuk mengatur delay dari sensor sonar
static int16_t sensor_task() {
	i2c_transmit(ALAMAT_SENSOR, 0, 81);					// mengatur alamat dari sensor sonar
	_delay_ms(65);										// delay untuk memastikan gelombang sudah kembali
	last_value = (i2c_read(ALAMAT_SENSOR, 2) << 8)		// mengambil nilai dari sensor sonar
	| (i2c_read(ALAMAT_SENSOR, 3) << 0);
	
	if(passwordIterator < 4) {							// jika password belum terisi semua
		lcd_set_ddram_addr(DDRAM_AWAL_PASS + 0x2);		// akan bergeser ke kenan pointernya
		int temp = last_value / 5;						// dan akan menuliskan nilai antara 1-9,
		if (temp <= 9) printf("%d", temp);				// tergantung dari jarak ke sensor sonar
		else printf("%d", 9);
	}
	else {
		if(inChPass) {
			lcd_set_ddram_addr(DDRAM_AWAL_NEW_PASS + 0x2);		// akan bergeser ke kenan pointernya
			int temp = last_value / 5;							// dan akan menuliskan nilai antara 1-9,
			if (temp <= 9) printf("%d", temp);					// tergantung dari jarak ke sensor sonar
			else printf("%d", 9);
		}
	}

	return 10;											// mengembalikan delay baru
}

// task untuk mengatur delay dan input
// dari tombol switch
static int16_t tombol_task() {
	uint8_t current_fast_btn = bit_is_clear(BTN_PIN, BTN_FAST);
	uint8_t current_slow_btn = bit_is_clear(BTN_PIN, BTN_SLOW);
	
	if(!slow_btn && current_slow_btn && passwordIterator >= 4) {	// mengecek tombol untuk ganti password
		if(!inChPass) {												
			prepareChangePassword();								// masuk ke dalam interface ganti password
		}
		else {
			if(newPasswordIterator < 4) {							// melakukan pengisian password
				password_harusnya[newPasswordIterator++] = last_value / 5;
				DDRAM_AWAL_NEW_PASS = DDRAM_AWAL_NEW_PASS + 0x1;
				if(newPasswordIterator == 4) {
					lcd_clear();
					lcd_set_ddram_addr(0x00);
					printf("CHPASS");
					lcd_set_ddram_addr(0x40);
					printf("SUKSES");
					LED_PORT = 0x00;
					led_stop = 1;
					reset();
				}
			}
		}
	}

	if(!fast_btn && current_fast_btn) {								// mengecek tombol untuk input password
		if(passwordIterator < 4) {									// melakukan pengisian password
			password[passwordIterator++] = last_value / 5;
			DDRAM_AWAL_PASS = DDRAM_AWAL_PASS + 0x1;
			if(passwordIterator == 4) {								
				if(check_password(password, password_harusnya)) {	// mengecek isi password
					lcd_clear();									// perintah-perintah saat berhasil
					lcd_set_ddram_addr(0x00);
					printf("AKSES");
					lcd_set_ddram_addr(0x40);
					printf("DITERIMA");
					LED_PORT = 0x00;
					led_stop = 1;
					timeout_stop = 1;
				}
				else {												// perintah-perintah saat gagal
					lcd_clear();
					lcd_set_ddram_addr(0x00);
					printf("PASSWORD");
					lcd_set_ddram_addr(0x40);
					printf("SALAH");
					LED_PORT = 0x00;
					led_stop = 1;
					reset();
				}
			}
		}
	}
	fast_btn = current_fast_btn;
	slow_btn = current_slow_btn;
	
	return 10;
}

//sebuah task untuk mengukur waktu timeout
static int16_t timeout_task() {
	if(timeout_stop) {
		return INT16_MAX;
	}
	else {
		timeoutIterator++;
		if(timeoutIterator > 3) {					// jika sudah lebih dari 3 kali peningkatan kecepatan
			passwordIterator = 4;					// perintah-perintah jika waktu habis
			lcd_clear();
			lcd_set_ddram_addr(0x00);
			printf("WAKTU");
			lcd_set_ddram_addr(0x40);
			printf("HABIS");
			LED_PORT = 0x00;
			led_stop = 1;
			exit(0);
		}
		else {
			led_delay = 300 - timeoutIterator * 99;	// perintah-perintah pengingkatan kecepatan
			return 10000;
		}
	}
}

// task untuk menampilkan interface ganti password
static int16_t chpass_task() {
	lcd_clear();
	lcd_set_ddram_addr(0x00);
	printf("NEW PASS");
	lcd_set_ddram_addr(0x42);
	printf("____");
	inChPass = 1;
	led_stop = 1;
	
	return INT16_MAX;
}

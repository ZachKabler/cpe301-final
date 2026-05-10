// Zach Kabler
// Group 13


#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdlib.h>


enum SystemState {
  STATE_OFF,
  STATE_IDLE,
  STATE_ACTIVE,
  STATE_ERROR
};

volatile bool onButtonPressed = false;

SystemState currentState = STATE_OFF;
SystemState previousState = STATE_ERROR;

const int LIGHT_ACTIVE_THRESHOLD = 700;
const int LIGHT_SAFE_THRESHOLD = 600;

// Timing
unsigned long lastLCDUpdate = 0;
unsigned long lastLogUpdate = 0;

const unsigned long LCD_UPDATE_INTERVAL = 1000;    // 1 second
const unsigned long LOG_UPDATE_INTERVAL = 60000;   // 1 minute


#define OFF_LED_BIT     PC7
#define IDLE_LED_BIT    PC6
#define ACTIVE_LED_BIT  PC5
#define ERROR_LED_BIT   PC4
#define ON_BUTTON_BIT     PE4
#define OFF_BUTTON_BIT    PD7
#define RESET_BUTTON_BIT  PG2
#define SERVO_BIT PB5
#define LCD_RS PA0
#define LCD_EN PA1
#define LCD_D4 PA2
#define LCD_D5 PA3
#define LCD_D6 PA4
#define LCD_D7 PA5

void uart0_init(unsigned long baud) {
  unsigned int ubrr = (F_CPU / 16 / baud) - 1;

  UBRR0H = (unsigned char)(ubrr >> 8);
  UBRR0L = (unsigned char)ubrr;

  UCSR0B = (1 << TXEN0) | (1 << RXEN0);
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void uart0_putchar(char c) {
  while (!(UCSR0A & (1 << UDRE0))) {
  }
  UDR0 = c;
}

void uart0_print(const char *str) {
  while (*str) {
    uart0_putchar(*str++);
  }
}

void uart0_println(const char *str) {
  uart0_print(str);
  uart0_print("\r\n");
}

void uart0_print_int(int value) {
  char buffer[12];
  itoa(value, buffer, 10);
  uart0_print(buffer);
}


void adc_init() {

  ADMUX = (1 << REFS0);

  ADCSRA = (1 << ADEN) |
           (1 << ADPS2) |
           (1 << ADPS1) |
           (1 << ADPS0);

  ADCSRB = 0;
}

int adc_read_A0() {

  ADMUX = (ADMUX & 0xF0) | 0;

  ADCSRA |= (1 << ADSC);

  while (ADCSRA & (1 << ADSC)) {
  }

  return ADC;
}

void lcd_pulse_enable() {
  PORTA |= (1 << LCD_EN);
  _delay_us(1);
  PORTA &= ~(1 << LCD_EN);
  _delay_us(100);
}

void lcd_send4(uint8_t data) {
  // Clear PA2-PA5
  PORTA &= ~((1 << LCD_D4) |
             (1 << LCD_D5) |
             (1 << LCD_D6) |
             (1 << LCD_D7));

  if (data & 0x01) PORTA |= (1 << LCD_D4);
  if (data & 0x02) PORTA |= (1 << LCD_D5);
  if (data & 0x04) PORTA |= (1 << LCD_D6);
  if (data & 0x08) PORTA |= (1 << LCD_D7);

  lcd_pulse_enable();
}

void lcd_command(uint8_t cmd) {
  PORTA &= ~(1 << LCD_RS);

  lcd_send4(cmd >> 4);
  lcd_send4(cmd & 0x0F);

  _delay_ms(2);
}

void lcd_data(uint8_t data) {
  PORTA |= (1 << LCD_RS);

  lcd_send4(data >> 4);
  lcd_send4(data & 0x0F);

  _delay_us(100);
}

void lcd_print(const char *str) {
  while (*str) {
    lcd_data(*str++);
  }
}

void lcd_print_int(int value) {
  char buffer[8];
  itoa(value, buffer, 10);
  lcd_print(buffer);
}

void lcd_clear() {
  lcd_command(0x01);
  _delay_ms(2);
}

void lcd_set_cursor(uint8_t row, uint8_t col) {
  uint8_t address;

  if (row == 0) {
    address = 0x00 + col;
  } else {
    address = 0x40 + col;
  }

  lcd_command(0x80 | address);
}

void lcd_init() {
  
  DDRA |= (1 << LCD_RS) |
          (1 << LCD_EN) |
          (1 << LCD_D4) |
          (1 << LCD_D5) |
          (1 << LCD_D6) |
          (1 << LCD_D7);

  _delay_ms(50);

  PORTA &= ~(1 << LCD_RS);

  lcd_send4(0x03);
  _delay_ms(5);
  lcd_send4(0x03);
  _delay_us(150);
  lcd_send4(0x03);
  lcd_send4(0x02);

  lcd_command(0x28); // 4-bit, 2 line
  lcd_command(0x0C); // display on, cursor off
  lcd_command(0x06); // entry mode
  lcd_clear();
}

void servo_init() {
  
  DDRB |= (1 << SERVO_BIT);

  TCCR1A = (1 << COM1A1) | (1 << WGM11);
  TCCR1B = (1 << WGM13) |
           (1 << WGM12) |
           (1 << CS11); 

  ICR1 = 39999;

  OCR1A = 3000;
}

void blinds_open() {

  OCR1A = 5000;
}

void blinds_close() {

  OCR1A = 4000;
}

void blinds_safe() {

  OCR1A = 3000;
}

void io_init() {
  
  DDRC |= (1 << OFF_LED_BIT) |
          (1 << IDLE_LED_BIT) |
          (1 << ACTIVE_LED_BIT) |
          (1 << ERROR_LED_BIT);

  DDRD &= ~(1 << OFF_BUTTON_BIT);
  PORTD |= (1 << OFF_BUTTON_BIT);

  DDRG &= ~(1 << RESET_BUTTON_BIT);
  PORTG |= (1 << RESET_BUTTON_BIT);

  DDRE &= ~(1 << ON_BUTTON_BIT);
  PORTE |= (1 << ON_BUTTON_BIT);
}

bool off_button_pressed() {

  return !(PIND & (1 << OFF_BUTTON_BIT));
}

bool reset_button_pressed() {

  return !(PING & (1 << RESET_BUTTON_BIT));
}

void set_state_leds(SystemState state) {

  PORTC &= ~((1 << OFF_LED_BIT) |
             (1 << IDLE_LED_BIT) |
             (1 << ACTIVE_LED_BIT) |
             (1 << ERROR_LED_BIT));

  if (state == STATE_OFF) {
    PORTC |= (1 << OFF_LED_BIT);
  } 
  else if (state == STATE_IDLE) {
    PORTC |= (1 << IDLE_LED_BIT);
  } 
  else if (state == STATE_ACTIVE) {
    PORTC |= (1 << ACTIVE_LED_BIT);
  } 
  else if (state == STATE_ERROR) {
    PORTC |= (1 << ERROR_LED_BIT);
  }
}

void interrupt_init() {
  cli();

  EICRB |= (1 << ISC41);
  EICRB &= ~(1 << ISC40);

  EIMSK |= (1 << INT4);

  sei();
}

ISR(INT4_vect) {
  onButtonPressed = true;
}

void print_timestamp() {
  unsigned long totalSeconds = millis() / 1000;

  int hours = (totalSeconds / 3600) % 24;
  int minutes = (totalSeconds / 60) % 60;
  int seconds = totalSeconds % 60;

  if (hours < 10) uart0_putchar('0');
  uart0_print_int(hours);
  uart0_putchar(':');

  if (minutes < 10) uart0_putchar('0');
  uart0_print_int(minutes);
  uart0_putchar(':');

  if (seconds < 10) uart0_putchar('0');
  uart0_print_int(seconds);

  uart0_print(" - ");
}

void log_event(const char *message) {
  print_timestamp();
  uart0_println(message);
}

void log_light_reading(int lightValue) {
  print_timestamp();
  uart0_print("Light reading: ");
  uart0_print_int(lightValue);
  uart0_println("");
}

void update_lcd(SystemState state, int lightValue) {
  lcd_clear();

  if (state == STATE_OFF) {
    lcd_set_cursor(0, 0);
    lcd_print("System OFF");

    lcd_set_cursor(1, 0);
    lcd_print("Blinds Safe");
  }

  else if (state == STATE_IDLE) {
    lcd_set_cursor(0, 0);
    lcd_print("State: IDLE");

    lcd_set_cursor(1, 0);
    lcd_print("Light: ");
    lcd_print_int(lightValue);
  }

  else if (state == STATE_ACTIVE) {
    lcd_set_cursor(0, 0);
    lcd_print("State: ACTIVE");

    lcd_set_cursor(1, 0);
    lcd_print("Light: ");
    lcd_print_int(lightValue);
  }

  else if (state == STATE_ERROR) {
    lcd_set_cursor(0, 0);
    lcd_print("ERROR");

    lcd_set_cursor(1, 0);
    lcd_print("Sensor Fault");
  }
}

void handle_state_entry(SystemState state) {
  set_state_leds(state);

  if (state == STATE_OFF) {
    blinds_safe();
    log_event("OFF state entered");
  }

  else if (state == STATE_IDLE) {
    blinds_open();
    log_event("IDLE state entered");
    log_event("Blinds opening / normal position");
  }

  else if (state == STATE_ACTIVE) {
    blinds_close();
    log_event("ACTIVE state entered");
    log_event("Blinds closing due to high light");
  }

  else if (state == STATE_ERROR) {
    blinds_safe();
    log_event("ERROR: Sensor fault");
    log_event("Blinds moved to safe position");
  }
}

void setup() {
  uart0_init(9600);
  adc_init();
  io_init();
  lcd_init();
  servo_init();
  interrupt_init();

  currentState = STATE_OFF;
  previousState = STATE_ERROR;

  log_event("System initialized");
}

void loop() {
  int lightValue = 0;

  if (off_button_pressed()) {
    currentState = STATE_OFF;
  }

  if (reset_button_pressed()) {
    currentState = STATE_IDLE;
  }

  if (onButtonPressed) {
    onButtonPressed = false;

    if (currentState == STATE_OFF) {
      currentState = STATE_IDLE;
    }
  }

  if (currentState == STATE_IDLE || currentState == STATE_ACTIVE) {
    lightValue = adc_read_A0();

    if (lightValue <= 5 || lightValue >= 1018) {
      currentState = STATE_ERROR;
    }
  }

  if (currentState == STATE_IDLE) {
    if (lightValue > LIGHT_ACTIVE_THRESHOLD) {
      currentState = STATE_ACTIVE;
    }
  }

  else if (currentState == STATE_ACTIVE) {
    if (lightValue < LIGHT_SAFE_THRESHOLD) {
      currentState = STATE_IDLE;
    }
  }

  else if (currentState == STATE_ERROR) {
    blinds_safe();
  }

  else if (currentState == STATE_OFF) {
    blinds_safe();
  }

  if (currentState != previousState) {
    handle_state_entry(currentState);
    previousState = currentState;
    update_lcd(currentState, lightValue);
  }

  if (millis() - lastLCDUpdate >= LCD_UPDATE_INTERVAL) {
    lastLCDUpdate = millis();
    update_lcd(currentState, lightValue);
  }

  if ((currentState == STATE_IDLE || currentState == STATE_ACTIVE) &&
      (millis() - lastLogUpdate >= LOG_UPDATE_INTERVAL)) {
    lastLogUpdate = millis();
    log_light_reading(lightValue);
  }
}
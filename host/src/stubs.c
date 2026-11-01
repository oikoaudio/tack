// No-op stand-ins for the UI, sequencer, UART and USB code the sound engine links against.
#include <stdint.h>

void lfo_recalcSync(void);

volatile uint32_t systick_ticks;
uint8_t menu_activePage;
uint8_t frontParser_activeTrack;
char seq_patternSet[1 << 20];

float adc_getFaderVolume(uint8_t idx) { (void)idx; return 1.0f; }

void uart_sendFrontpanelByte(uint8_t b) { (void)b; }
void uart_sendMidi() {}
void usb_sendMidi() {}

// the sequencer isn't here: the host's tempo is what an LFO in sync follows
static uint16_t hostBpm = 120;
uint16_t seq_getBpm(void) { return hostBpm; }

void lxr_host_setTempo(double bpm)
{
	const uint16_t rounded = bpm < 1 ? 1 : bpm > 999 ? 999 : (uint16_t)(bpm + 0.5);
	if(rounded == hostBpm) return;
	hostBpm = rounded;
	lfo_recalcSync();
}
uint8_t seq_getExtSync(void) { return 0; }
uint8_t seq_isRunning(void) { return 0; }
uint8_t seq_isTrackMuted(uint8_t t) { (void)t; return 0; }
void seq_setRunning() {}
void seq_setMute() {}
void seq_midiNoteOff() {}
void seq_sendMidiNoteOn() {}
void seq_sync() {}
void seq_setNextPattern() {}
void seq_recordAutomation() {}
void seq_addNote() {}
void sync_midiStartStop() {}
void sync_midiResetPosition() {}

void menu_switchSubPage() {}
void menu_repaint(void) {}
uint8_t menu_getViewedPattern(void) { return 0; }
uint8_t menu_getSubPage(void) { return 0; }
uint8_t menu_getActiveVoice(void) { return 0; }

typedef struct { int dummy; } RNG_HandleTypeDef_stub;
RNG_HandleTypeDef_stub hrng;

/* PC-sim stubs for radio HAL functions (no hardware on PC). */
#ifndef ARDUINO
#include "hal_interface.h"

int16_t hw_set_radio_params(radio_params_t &params) { (void)params; return 0; }
void    hw_get_radio_params(radio_params_t &params) { (void)params; }
void    hw_set_radio_listening() {}
void    hw_set_radio_default() {}
void    hw_radio_sleep() {}
void    hw_set_radio_tx(radio_tx_params_t &params, bool continuous) { (void)params; (void)continuous; }
void    hw_get_radio_rx(radio_rx_params_t &params) { (void)params; }

const char *radio_get_freq_list() { return "N/A"; }
const char *radio_get_bandwidth_list(bool high_freq) { (void)high_freq; return "N/A"; }
const char *radio_get_tx_power_list(bool high_freq) { (void)high_freq; return "N/A"; }
float radio_get_freq_from_index(uint8_t index) { (void)index; return 0.0f; }
float radio_get_bandwidth_from_index(uint8_t index) { (void)index; return 0.0f; }
float radio_get_tx_power_from_index(uint8_t index) { (void)index; return 0.0f; }
uint16_t radio_get_freq_length() { return 0; }
uint16_t radio_get_bandwidth_length() { return 0; }
uint16_t radio_get_tx_power_length() { return 0; }
bool radio_transmit(const uint8_t *data, size_t length) { (void)data; (void)length; return false; }
#endif

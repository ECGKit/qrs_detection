#include <msp430.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "drivers.h"
#include "circular_buffer.h"
#include "moving_sum.h"
#include "moving_average.h"

#define LP_BUF_SIZE 6
#define HP_BUF_SIZE 32
#define DERIV_BUF_SIZE 5
#define INTEG_BUF_SIZE 30
#define HR_BUF_SIZE 8

MOVING_SUM(qrs_lp, unsigned, LP_BUF_SIZE);
CIRCULAR_BUFFER(qrs_hp, unsigned, HP_BUF_SIZE);
CIRCULAR_BUFFER(qrs_deriv, int, DERIV_BUF_SIZE);
MOVING_SUM(qrs_integ, unsigned,INTEG_BUF_SIZE);
MOVING_AVERAGE(qrs_rr, unsigned, HR_BUF_SIZE);

// ------------------
struct qrs_lpf {
	struct moving_sum_qrs_lp s0;
	struct moving_sum_qrs_lp s1;
};

void qrs_lpf_init(struct qrs_lpf *self) {
	moving_sum_qrs_lp_init(&self->s0);
	moving_sum_qrs_lp_init(&self->s1);
}

void qrs_lpf_push(struct qrs_lpf *self, unsigned value) {
	moving_sum_qrs_lp_input(&self->s0, value);
	moving_sum_qrs_lp_input(&self->s1, moving_sum_qrs_lp_last_output(&self->s0));
}

unsigned qrs_lpf_output(struct qrs_lpf *self) {
	return moving_sum_qrs_lp_last_output(&self->s1);
}

// ------------------
struct qrs_hpf {
	struct cbuf_qrs_hp buf;
	int output;
};

void qrs_hpf_init(struct qrs_hpf *self) {
	cbuf_qrs_hp_init(&self->buf);
	self->output = 0;
}

void qrs_hpf_push(struct qrs_hpf *self, unsigned value) {

	unsigned x32 = cbuf_qrs_hp_oldest(&self->buf);

	cbuf_qrs_hp_input(&self->buf, value);
	unsigned x0 = cbuf_qrs_hp_last_input(&self->buf);
	unsigned x16 = cbuf_qrs_hp_nth_oldest(&self->buf, 16);
	unsigned x17 = cbuf_qrs_hp_nth_oldest(&self->buf, 17);

	unsigned t1 = (x0 >> 5) + x17;
	unsigned t2 = (x32 >> 5) + x16;
	self->output = (self->output + t2) - t1;
}

int qrs_hpf_output(struct qrs_hpf *self) {
	return self->output;
}

// ------------------
struct qrs_deriv {
	struct cbuf_qrs_deriv buf;
};

void qrs_deriv_init(struct qrs_deriv *self) {
	cbuf_qrs_deriv_init(&self->buf);
}

void qrs_deriv_push(struct qrs_deriv *self, int value) {
	cbuf_qrs_deriv_input(&self->buf, value);
}

int qrs_deriv_output(struct qrs_deriv *self) {
	int x0 = cbuf_qrs_deriv_last_input(&self->buf);
	int x1 = cbuf_qrs_deriv_nth_oldest(&self->buf, 1);
	int x3 = cbuf_qrs_deriv_nth_oldest(&self->buf, 3);
	int x4 = cbuf_qrs_deriv_oldest(&self->buf);

	int t1 = (x0 << 1) + (x1);
	int t2 = (x3 + (x4 << 1));

	return (t1 - t2) >> 3;
}

// ------------------
struct qrs_integ {
	struct moving_sum_qrs_integ buf;
};

void qrs_integ_init(struct qrs_integ *self) {
	moving_sum_qrs_integ_init(&self->buf);
}

void qrs_integ_push(struct qrs_integ *self, unsigned value) {
	moving_sum_qrs_integ_input(&self->buf, value);
}

unsigned qrs_integ_output(struct qrs_integ *self) {
	unsigned f30_out = moving_sum_qrs_integ_last_output(&self->buf);
	unsigned f32_out = f30_out + (f30_out >> 4);
	return f32_out >> 5;
}

// ------------------
enum qrs_det_state {
	RISING,
	FLAT,
	FALLING
};

struct qrs_peak_det {
	enum qrs_det_state state;
	enum qrs_det_state last_state;
	unsigned last_value;
};

void qrs_peak_det_init(struct qrs_peak_det *det) {
	det->state = FLAT;
	det->last_value = 0;
}

bool qrs_peak_det_identify(struct qrs_peak_det *det, unsigned value) {
	bool is_peak = false;

	enum qrs_det_state next;
	if (value > det->last_value) next = RISING;
	else if (value < det->last_value) next = FALLING;
	else next = FLAT;

	if (next == FLAT && det->state == RISING) det->last_state = RISING;
	else det->last_state = det->state;

	if (det->state == RISING && next == FALLING) is_peak = true;
	else if (det->state == FALLING && next == RISING) is_peak = true;
	else if (det->last_state == RISING && det->state == FLAT && next == FALLING) is_peak = true;

	det->state = next;
	det->last_value = value;

	return is_peak;
}

// ------------------
enum qrs_peak_type {
	PEAK_SIGNAL,
	PEAK_NOISE,
	PEAK_NONE
};

struct qrs_peak_class {
	struct qrs_peak_det det;

	unsigned sig_peak;
	unsigned noise_peak;

	unsigned thresh1;
	unsigned thresh2;
};

void qrs_peak_class_init(struct qrs_peak_class *cls) {
	qrs_peak_det_init(&cls->det);

	cls->sig_peak = 0;
	cls->noise_peak = 0;
	cls->thresh1 = 0;
	cls->thresh2 = 0;
}

enum qrs_peak_type qrs_peak_integ_classify(struct qrs_peak_class *cls, struct qrs_integ *wave) {

	// get the latest output
	unsigned latest_value  = qrs_integ_output(wave);

	// check if that output results in a peak
	if (qrs_peak_det_identify(&cls->det, latest_value)) {

		bool found = false;

		cls->thresh1 = cls->noise_peak + ((cls->sig_peak - cls->noise_peak) >> 2);
		cls->thresh2 = cls->thresh1 >> 1;

		if (latest_value > cls->thresh1) {
			cls->sig_peak = (latest_value + (7 * cls->sig_peak)) >> 3;
			return PEAK_SIGNAL;
		} else {	// searchback

			struct qrs_peak_det searchback;


			qrs_peak_det_init(&searchback);

			signed it = wave->buf.buf.write;
			unsigned loop_counter;

			for (loop_counter = 0; loop_counter < INTEG_BUF_SIZE; loop_counter++) {
				unsigned sample = wave->buf.buf.samples[it];

				if (qrs_peak_det_identify(&searchback, sample) && sample > cls->thresh2) {
					found = true;
					break;
				}

				it--;
				if (it < 0) it = INTEG_BUF_SIZE - 1;
			}

			// else it's a noise peak or we found it late so it's still a signal peak
			if (!found) cls->noise_peak = (latest_value + (7 * cls->noise_peak)) >> 3;
			else cls->sig_peak = (latest_value + (3 *  cls->sig_peak)) >> 2;
		}

		if (!found) return PEAK_NOISE;
		else return PEAK_SIGNAL;
	}

	return PEAK_NONE;
}

enum qrs_peak_type qrs_peak_filter_classify(struct qrs_peak_class *cls, struct qrs_hpf *wave) {

	// get the latest output
	unsigned latest_value  = qrs_hpf_output(wave);

	// check if that output results in a peak
	if (qrs_peak_det_identify(&cls->det, latest_value)) {

		bool found = false;

		cls->thresh1 = cls->noise_peak + ((cls->sig_peak - cls->noise_peak) >> 2);
		cls->thresh2 = cls->thresh1 >> 1;

		if (latest_value > cls->thresh1) {
			cls->sig_peak = (latest_value + (7 * cls->sig_peak)) >> 3;
			return PEAK_SIGNAL;
		} else {	// searchback

			struct qrs_peak_det searchback;

			qrs_peak_det_init(&searchback);

			signed it = wave->buf.write;
			unsigned loop_counter;

			for (loop_counter = 0; loop_counter < DERIV_BUF_SIZE; loop_counter++) {
				unsigned sample = wave->buf.samples[it];

				if (qrs_peak_det_identify(&searchback, sample) && sample > cls->thresh2) {
					found = true;
					break;
				}

				it--;
				if (it < 0) it = DERIV_BUF_SIZE - 1;
			}

			// else it's a noise peak or we found it late so it's still a signal peak
			if (!found) cls->noise_peak = (latest_value + (7 * cls->noise_peak)) >> 3;
			else cls->sig_peak = (latest_value + (3 *  cls->sig_peak)) >> 2;
		}

		if (!found) return PEAK_NOISE;
		else return PEAK_SIGNAL;
	}

	return PEAK_NONE;
}

// ------------------
struct qrs_rr_avg {
	struct maf_qrs_rr avg1;
	struct maf_qrs_rr avg2;

	unsigned rr_low_lim;
	unsigned rr_high_lim;
	unsigned rr_missed_lim;
};

void qrs_rr_avg_init(struct qrs_rr_avg *self) {
	maf_qrs_rr_init(&self->avg1);
	maf_qrs_rr_init(&self->avg2);
}

bool qrs_rr_avg_push(struct qrs_rr_avg *self, unsigned time) {
	maf_qrs_rr_input(&self->avg1, time);

	unsigned rr_avg2 = maf_qrs_rr_last_output(&self->avg1);

	self->rr_low_lim = (15 * rr_avg2) >> 4;
	self->rr_high_lim = (rr_avg2 + (rr_avg2 << 3)) >> 3;
	self->rr_missed_lim = (rr_avg2 + (rr_avg2 << 2) + (rr_avg2 << 3)) >> 3;

	if (time >= self->rr_low_lim && time <= self->rr_high_lim) {
		maf_qrs_rr_input(&self->avg2, time);
		return true;
	} else if (time > self->rr_missed_lim) {
		return true;
	}

	return false;
}

// ------------------
enum qrs_state {
	WAVE_QRS,
	WAVE_T
};

// ------------------
int main(void) {
	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer

	P1DIR |= (BIT0 | BIT6);
	P1OUT &= ~(BIT0 | BIT6);

	unsigned voltage = 0;

	struct qrs_lpf pan_lpf;
	struct qrs_hpf pan_hpf;
	struct qrs_deriv pan_deriv;
	int qrs_filter_out;
	unsigned qrs_filter_sq;

	struct qrs_integ pan_integ;

	struct qrs_peak_class integ_class;
	struct qrs_peak_class filter_class;

	struct qrs_rr_avg beat_check;

	_disable_interrupts();

	uart_init();
	clock_init();
	adc_init(&voltage, 1);
	timer_init(29);

	qrs_lpf_init(&pan_lpf);
	qrs_hpf_init(&pan_hpf);
	qrs_deriv_init(&pan_deriv);

	qrs_integ_init(&pan_integ);

	qrs_peak_class_init(&integ_class);
	qrs_peak_class_init(&filter_class);

	qrs_rr_avg_init(&beat_check);

	_enable_interrupts();

	enum qrs_state state = WAVE_QRS;
	unsigned prev_beat = 0;
	unsigned time = 0;
	int last_qrs;
	while (1) {
		P1OUT ^= BIT0;

		time++;

		qrs_lpf_push(&pan_lpf, voltage);
		qrs_hpf_push(&pan_hpf, qrs_lpf_output(&pan_lpf));
		qrs_deriv_push(&pan_deriv, qrs_hpf_output(&pan_hpf));

		qrs_filter_out = qrs_deriv_output(&pan_deriv);
		qrs_filter_sq = qrs_filter_out * qrs_filter_out;
		if (qrs_filter_sq >= 255) qrs_filter_sq = 255;

		qrs_integ_push(&pan_integ, qrs_filter_sq);

		enum qrs_peak_type integ_peak = qrs_peak_integ_classify(&integ_class, &pan_integ);
		enum qrs_peak_type filter_peak = qrs_peak_filter_classify(&filter_class, &pan_hpf);

		if (integ_peak == PEAK_SIGNAL && integ_peak == filter_peak) {
			unsigned dt = time - prev_beat;
			prev_beat = time;

			bool valid_qrs = qrs_rr_avg_push(&beat_check, dt);

			if (state == WAVE_T && valid_qrs) {
				last_qrs = qrs_filter_out;
				state = WAVE_QRS;
			} else if (state == WAVE_QRS && qrs_filter_out <= (last_qrs >> 1)) {
				state = WAVE_T;
			}

			uart_putc((char) state);
		}

		P1OUT ^= BIT0;

		// if (FIFO EMPTY)
		_low_power_mode_3();
	}

	return 0;
}


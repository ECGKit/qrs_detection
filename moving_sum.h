/*
 * moving_sum.h
 *
 *  Created on: Apr 30, 2020
 *      Author: krad2
 */

#ifndef MOVING_SUM_H_
#define MOVING_SUM_H_

#include "circular_buffer.h"

#define MOVING_SUM(name, type, size)															\
CIRCULAR_BUFFER(name, type, size);																\
struct moving_sum_##name {																		\
	struct cbuf_##name buf;																		\
	type sum;																					\
};																								\
static inline void moving_sum_##name##_reset(struct moving_sum_##name *self, type value) {		\
	cbuf_##name##_reset(&self->buf, value);														\
	self->sum = size * value;																	\
}																								\
static inline void moving_sum_##name##_init(struct moving_sum_##name *self) {					\
	cbuf_##name##_init(&self->buf);																\
	self->sum = 0;																				\
}																								\
static inline void moving_sum_##name##_input(struct moving_sum_##name *self, type input) {   	\
    type oldest = cbuf_##name##_oldest(&self->buf);												\
    cbuf_##name##_input(&self->buf, input);														\
	self->sum += (input - oldest);																\
}																								\
static inline type moving_sum_##name##_last_output(const struct moving_sum_##name *self) {   	\
	return self->sum;																			\
}                                                                                       		\
static inline type moving_sum_##name##_last_input(const struct moving_sum_##name *self) {    	\
	return cbuf_##name_##last_input(&self->buf);												\
}																								\

#endif /* MOVING_SUM_H_ */

/*
 * moving_average.h
 *
 *  Created on: May 1, 2020
 *      Author: krad2
 */

#ifndef MOVING_AVERAGE_H_
#define MOVING_AVERAGE_H_

#include "moving_sum.h"

#define MOVING_AVERAGE(name, type, size)														\
MOVING_SUM(name, type, size);																	\
struct maf_##name {																				\
	struct moving_sum_##name buf;																\
	type average;																				\
};																								\
static inline void maf_##name##_reset(struct maf_##name *self, type value) {					\
	moving_sum_##name##_reset(&self->buf, value);												\
	self->average = value;																		\
}																								\
static inline void maf_##name##_init(struct maf_##name *self) {									\
	moving_sum_##name##_init(&self->buf);														\
	self->average = 0;																			\
}																								\
static inline void maf_##name##_input(struct maf_##name *self, type input) {   					\
    moving_sum_##name##_input(&self->buf, input);												\
	self->average = moving_sum_##name##_last_output(&self->buf) / size;							\
}																								\
static inline type maf_##name##_last_output(const struct maf_##name *self) {   					\
	return self->average;																		\
}                                                                                       		\
static inline type maf_##name##_last_input(const struct maf_##name *self) {    					\
	return moving_sum_##name_##last_input(&self->buf);											\
}																								\

#endif /* MOVING_AVERAGE_H_ */

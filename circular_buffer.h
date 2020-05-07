/*
 * circular_buffer.h
 *
 *  Created on: Apr 30, 2020
 *      Author: krad2
 */

#ifndef CIRCULAR_BUFFER_H_
#define CIRCULAR_BUFFER_H_

#include <stddef.h>

#define CIRCULAR_BUFFER(name, type, size)															\
struct cbuf_##name {																				\
    type samples[size];																				\
    size_t write;																					\
};																									\
static inline void cbuf_##name##_reset(struct cbuf_##name *self, type value) {						\
	size_t i;																						\
	for (i = 0; i < size; ++i) {																	\
		self->samples[i] = value;																	\
	}																								\
}																									\
static inline void cbuf_##name##_init(struct cbuf_##name *self) {									\
    self->write = 0;																				\
    cbuf_##name##_reset(self, 0);																	\
}																									\
static inline void cbuf_##name##_input(struct cbuf_##name *self, type input) {   					\
    self->samples[self->write] = input;                                                 			\
    self->write++;                                                                      			\
    if (self->write == size) self->write = 0;														\
}																									\
static inline type cbuf_##name##_oldest(const struct cbuf_##name *self) {   						\
    size_t index = self->write;																		\
    return self->samples[index];                                                        			\
}                                                                                       			\
static inline type cbuf_##name##_nth_oldest(const struct cbuf_##name *self, size_t n) {				\
	size_t index;																					\
	if (self->write < (n + 1)) index = self->write + size - (n + 1);								\
	else index = self->write - (n + 1);																\
	return self->samples[index];																	\
}																									\
static inline type cbuf_##name##_last_input(const struct cbuf_##name *self) {    					\
    return cbuf_##name##_nth_oldest(self, 0);														\
}																									\

#endif /* CIRCULAR_BUFFER_H_ */

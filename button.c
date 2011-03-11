#include "button.h"

#include <types/types.h>
#include <skel-usb.h>

#include <math.h>

char button_flags[5];



// last 16 buttons values
static unsigned int old_b[5][16];
// Insertion index
static unsigned char insert[5]; 
// Maximum value of the 16 
static unsigned int max[5]; 
// Minimum value of the 16
static unsigned int min[5];
// sum of the 16
static unsigned long sum[5];
// Mean IIR filter. 
static unsigned  long sum_filtered[5];
// IIR filter for the "noise".
static unsigned long noise[5];

// Some statemachine variables
static unsigned char inhibit[5];

// The binary status of the button
#define DEBOUNCE 3
#define STATE_RELEASED (-1)
static char state[5];

static unsigned char init[5];

// Some offset, constants, etc ...
#define MIN_TRESHOLD 200
#define TRESH_OFFSET 20

static void iir_sum(unsigned int i) {
	// Fixed point 26.6
	unsigned long u  = sum[i]*64;
	
	// TODO CHECK that compiler optimize correctly
	sum_filtered[i] = sum_filtered[i] * 128 + u;
	
	
	sum_filtered[i] = __udiv3216(sum_filtered[i], 129);
}

static void iir_noise(unsigned int i) {
	// Fixed point 26.6
	unsigned long u = __builtin_muluu(max[i] - min[i], 512);
	
	noise[i] = noise[i] * 128 + u;
	
	noise[i] = __udiv3216(noise[i], 129);
}

// update the max, min & mean value
static void compute_stats(unsigned int b, unsigned int i) {

	int j;
	
	// adjust the sum and insert now button value
	sum[i] -= old_b[i][insert[i]];
	sum[i] += b;
	old_b[i][insert[i]] = b;
	
	insert[i] = (insert[i] + 1) & 0xF; // +1 modulo 16
	
		// recompute the max & min ... 
	max[i] = 0;
	min[i] = 0xFFFF;
	for(j = 0; j < 16; j++) {
		if(max[i] < old_b[i][j])
			max[i] = old_b[i][j];
		
		if(min[i] > old_b[i][j])
			min[i] = old_b[i][j];
	}
	
	iir_sum(i);
	iir_noise(i);
}

void button_process(unsigned int b, unsigned int i) {
	unsigned int tresh;
	
	/* first give raw value to the user... */
	vmVariables.buttons[i] = b;
	
	if(!init[i]) {
		init[i] = 1;
		int j; 
		for(j = 0; j < 16; j++)
			old_b[i][j] = b;
		noise[i] = 20*512;
		sum_filtered[i] = 64*16UL * b;
		sum[i] = 16UL * b;
	}
	
	vmVariables.buttons_mean[i] = sum_filtered[i] / (64*16);
	vmVariables.buttons_noise[i] = noise[i] / 256;
		
	tresh = noise[i]/256;
	if(tresh < MIN_TRESHOLD)
		tresh = MIN_TRESHOLD;
	
	tresh += TRESH_OFFSET;
	// FIXME Check the division optimisation (or use div32by16u())
	// Fixme, do the check only one every 16 samples ? 
	if(b < ((unsigned int)(sum_filtered[i]/(64*16))) - tresh) {
		state[i]++;
	} else {
		if(state[i] == DEBOUNCE)
			state[i] = STATE_RELEASED;
		else
			state[i] = 0;	
	}
	
	if(state[i] > DEBOUNCE) {
		state[i] = DEBOUNCE;
		if(!vmVariables.buttons_state[i])
			button_flags[i] = 1;
		vmVariables.buttons_state[i] = 1;
	} else {
		if(state[i] == STATE_RELEASED) {
			// We got released, inhibit any stat update for some time ...
			inhibit[i] = 16;
		}
		vmVariables.buttons_state[i] = 0;
		if(inhibit[i] > 0) 
			inhibit[i]--;
		else 
			if(!state[i])
				compute_stats(b,i);
	}
}

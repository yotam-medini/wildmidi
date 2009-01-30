/*
    reverb.c - reverb handling
    Copyright (C) 2001-2009 Chris Ison

    This file is part of WildMIDI.

    WildMIDI is free software: you can redistribute and/or modify the players
    under the terms of the GNU General Public License and you can redistribute
    and/or modify the library under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation, either version 3 of
   the licenses, or(at your option) any later version.

    WildMIDI is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License and
    the GNU Lesser General Public License for more details.

    You should have received a copy of the GNU General Public License and the
    GNU Lesser General Public License along with WildMIDI.  If not,  see
    <http://www.gnu.org/licenses/>.

    Email: wildcode@users.sourceforge.net

	$Id: reverb.c,v 1.2 2008/06/05 06:06:23 wildcode Exp $
*/

#include <math.h>
#include <stdlib.h>

#include "reverb.h"

#ifndef M_LN2
#define M_LN2	   0.69314718055994530942
#endif

#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif

/*
	reverb function
*/
inline void
reset_reverb (struct _rvb *rvb) {
	int i,j;
	for (i = 0; i < rvb->l_buf_size; i++) {
		rvb->l_buf[i] = 0;
	}
	for (i = 0; i < rvb->r_buf_size; i++) {
		rvb->r_buf[i] = 0;
	}
	for (i = 0; i < 6; i++) {
		for (j = 0; j < 2; j++) {
			rvb->l_buf_flt_in[i][j] = 0;
			rvb->l_buf_flt_out[i][j] = 0;
			rvb->r_buf_flt_in[i][j] = 0;
			rvb->r_buf_flt_out[i][j] = 0;
		}
	}
}

/*
	init_reverb
	
	=========================
	Engine Description
	
	8 reflective points around the room
	2 speaker positions
	1 listener position
	
	Sounds come from the speakers to all reflective points and to the listener.
	Sound comes from the reflective points to the listener.
	These sounds are combined, put through a filter that mimics surface absorbtion.
	The combined sounds are also sent to the reflective points on the opposite side.
	
*/

inline struct _rvb *
init_reverb(int rate) {
	struct _rvb *rtn_rvb = malloc(sizeof(struct _rvb));

	int i = 0;
	

	struct _coord {
       double x;
       double y;
	};

	struct _coord SPL = { 2.5, 5.0 }; // Left Speaker Position
	struct _coord SPR = { 7.5, 5.0 }; // Right Speaker Position
	struct _coord LSN = { 5.0, 15 }; // Listener Position
	// position of the reflective points
	struct _coord RFN[] = {
		{ 10.0, 0.0 },
		{ 0.0, 13.3333 },
		{ 0.0, 26.6666 },
		{ 10.0, 40.0 },
		{ 20.0, 40.0 },
		{ 30.0, 13.3333 },
		{ 30.0, 26.6666 },
		{ 20.0, 0.0 }
	}; 

	//distance 
	double SPL_DST[8];
	double SPR_DST[8];
	double RFN_DST[8];
	
	double MAXL_DST = 0.0;
	double MAXR_DST = 0.0;

	double SPL_LSN_XOFS = SPL.x - LSN.x;
	double SPL_LSN_YOFS = SPL.y - LSN.y;
	double SPL_LSN_DST = sqrtf((SPL_LSN_XOFS * SPL_LSN_XOFS) + (SPL_LSN_YOFS * SPL_LSN_YOFS));

	double SPR_LSN_XOFS = SPR.x - LSN.x;
	double SPR_LSN_YOFS = SPR.y - LSN.y;
	double SPR_LSN_DST = sqrtf((SPR_LSN_XOFS * SPR_LSN_XOFS) + (SPR_LSN_YOFS * SPR_LSN_YOFS));

	
	if (rtn_rvb == NULL) {
		return NULL;
	}

	/*
		setup Peaking band EQ filter
		filter based on public domain code by Tom St Denis
		http://www.musicdsp.org/showone.php?id=64
	*/
	for (i = 0; i < 6; i++) {
		/*
			filters set at 125Hz, 250Hz, 500Hz, 1000Hz, 2000Hz, 4000Hz 
		*/
		double Freq[] = {125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0};

		/*
			modify these to adjust the absorption qualities of the surface.
			Remember that lower frequencies are less effected by surfaces
		*/
		double dbGain[] = {-0.0, -6.0, -13.0, -21.0, -30.0, -40.0};
		double srate = (double)rate;
		double bandwidth = 2.0;
		double omega = 2.0 * M_PI * Freq[i] / srate;
		double sn = sin(omega);
		double cs = cos(omega);
		double alpha = sn * sinh(M_LN2 /2 * bandwidth * omega /sn);
		double A = pow(10.0, (dbGain[i] / 40.0));
		/*
			Peaking band EQ filter 
		*/
		double b0 = 1 + (alpha * A);
		double b1 = -2 * cs;
		double b2 = 1 - (alpha * A);
		double a0 = 1 + (alpha /A);
		double a1 = -2 * cs;
		double a2 = 1 - (alpha /A);
		
		rtn_rvb->coeff[i][0] = (signed long int)((b0 /a0) * 1024.0);
		rtn_rvb->coeff[i][1] = (signed long int)((b1 /a0) * 1024.0);
		rtn_rvb->coeff[i][2] = (signed long int)((b2 /a0) * 1024.0);
		rtn_rvb->coeff[i][3] = (signed long int)((a1 /a0) * 1024.0);
		rtn_rvb->coeff[i][4] = (signed long int)((a2 /a0) * 1024.0);
	}

	/* setup the reverb now */
	
	if (SPL_LSN_DST > MAXL_DST) MAXL_DST = SPL_LSN_DST;
	if (SPR_LSN_DST > MAXR_DST) MAXR_DST = SPR_LSN_DST;
	
	for (i = 0; i < 8; i++) {
		double SPL_RFL_XOFS = 0;
		double SPL_RFL_YOFS = 0;
		double SPR_RFL_XOFS = 0;
		double SPR_RFL_YOFS = 0;
		double RFN_XOFS = 0;
		double RFN_YOFS = 0;

		/* distance from listener to reflective surface */
		RFN_XOFS = LSN.x - RFN[i].x;
		RFN_YOFS = LSN.y - RFN[i].y;
		RFN_DST[i] = sqrtf((RFN_XOFS * RFN_XOFS) + (RFN_YOFS * RFN_YOFS));
		
		/* distance from speaker to 1st reflective surface */
		SPL_RFL_XOFS = SPL.x - RFN[i].x;
		SPL_RFL_YOFS = SPL.y - RFN[i].y;
		SPR_RFL_XOFS = SPR.x - RFN[i].x;
		SPR_RFL_YOFS = SPR.y - RFN[i].y;
		SPL_DST[i] = sqrtf((SPL_RFL_XOFS * SPL_RFL_XOFS) + (SPL_RFL_YOFS * SPL_RFL_YOFS));
		SPR_DST[i] = sqrtf((SPR_RFL_XOFS * SPR_RFL_XOFS) + (SPR_RFL_YOFS * SPR_RFL_YOFS));
		/* 
			add the 2 distances together and remove the speaker to listener distance
			so we dont have to delay the initial output
		*/
		SPL_DST[i] += RFN_DST[i];
		
		SPL_DST[i] -= SPL_LSN_DST;
		
		if (i < 4) {
			if (SPL_DST[i] > MAXL_DST) MAXL_DST = SPL_DST[i];
		} else {
			if (SPL_DST[i] > MAXR_DST) MAXR_DST = SPL_DST[i];
		}
    
		SPR_DST[i] += RFN_DST[i];
		
		SPR_DST[i] -= SPR_LSN_DST;
		if (i < 4) {
			if (SPR_DST[i] > MAXL_DST) MAXL_DST = SPR_DST[i];
		} else {
			if (SPR_DST[i] > MAXR_DST) MAXR_DST = SPR_DST[i];
		}
		
		
    
		/*
			Double the reflection distance so that we get the full distance traveled
		*/
		RFN_DST[i] *= 2.0;
		
		if (i < 4) {
			if (RFN_DST[i] > MAXL_DST) MAXL_DST = RFN_DST[i];
		} else {
			if (RFN_DST[i] > MAXR_DST) MAXR_DST = RFN_DST[i];
		}
	}

	/* init the reverb buffers */
	rtn_rvb->l_buf_size = (int)((float)rate * (MAXL_DST / 340.29));
	rtn_rvb->l_buf = malloc(sizeof(signed long int) * (rtn_rvb->l_buf_size + 1));
	rtn_rvb->l_out = 0;
	
	rtn_rvb->r_buf_size = (int)((float)rate * (MAXR_DST / 340.29));
	rtn_rvb->r_buf = malloc(sizeof(signed long int) * (rtn_rvb->r_buf_size + 1));
	rtn_rvb->r_out = 0;
	
	for (i=0; i< 4; i++) {
		rtn_rvb->l_sp_in[i] = (int)((float)rate * (SPL_DST[i] / 340.29));
		rtn_rvb->l_sp_in[i+4] = (int)((float)rate * (SPL_DST[i+4] / 340.29));
		rtn_rvb->r_sp_in[i] = (int)((float)rate * (SPR_DST[i] / 340.29));
		rtn_rvb->r_sp_in[i+4] = (int)((float)rate * (SPR_DST[i+4] / 340.29));
		rtn_rvb->l_in[i] = (int)((float)rate * (RFN_DST[i] / 340.29));
		rtn_rvb->r_in[i] = (int)((float)rate * (RFN_DST[i+4] / 340.29));
	}

	rtn_rvb->gain = 4;
	
	reset_reverb(rtn_rvb);
	return rtn_rvb;
}

/*
	free_reverb - free up memory used for reverb
*/
inline void
free_reverb (struct _rvb *rvb) {
	free (rvb->l_buf);
	free (rvb->r_buf);
	free (rvb);
}

inline void
do_reverb (struct _rvb *rvb, signed long int *buffer, int size) {
	int i, j;
	signed long int l_buf_flt = 0;
	signed long int r_buf_flt = 0;
	signed long int l_rfl = 0;
	signed long int r_rfl = 0;
	int vol_div = 32;
	
	for (i = 0; i < size; i += 2) {
		signed long int tmp_l_val = 0;
		signed long int tmp_r_val = 0;
		/*
			add the initial reflections
			from each speaker, 4 to go the left, 4 go to the right buffers
		*/
		tmp_l_val = buffer[i] / vol_div;
		tmp_r_val = buffer[i + 1] / vol_div;
		for (j = 0; j < 4; j++) {
			rvb->l_buf[rvb->l_sp_in[j]] += tmp_l_val;
			rvb->l_sp_in[j] = (rvb->l_sp_in[j] + 1) % rvb->l_buf_size;
			rvb->l_buf[rvb->r_sp_in[j]] += tmp_r_val;
			rvb->r_sp_in[j] = (rvb->r_sp_in[j] + 1) % rvb->l_buf_size;
				
			rvb->r_buf[rvb->l_sp_in[j+4]] += tmp_l_val;
			rvb->l_sp_in[j+4] = (rvb->l_sp_in[j+4] + 1) % rvb->r_buf_size;
			rvb->r_buf[rvb->r_sp_in[j+4]] += tmp_r_val;
			rvb->r_sp_in[j+4] = (rvb->r_sp_in[j+4] + 1) % rvb->r_buf_size;
		}

		/* 
			filter the reverb output and add to buffer
		*/
		l_rfl = rvb->l_buf[rvb->l_out];
		rvb->l_buf[rvb->l_out] = 0;
		rvb->l_out = (rvb->l_out + 1) % rvb->l_buf_size;
		
		r_rfl = rvb->r_buf[rvb->r_out];	
		rvb->r_buf[rvb->r_out] = 0;
		rvb->r_out = (rvb->r_out + 1) % rvb->r_buf_size;

		for (j = 0; j < 6; j++) {
			l_buf_flt = ((l_rfl * rvb->coeff[j][0]) +
				(rvb->l_buf_flt_in[j][0] * rvb->coeff[j][1]) +
				(rvb->l_buf_flt_in[j][1] * rvb->coeff[j][2]) -
				(rvb->l_buf_flt_out[j][0] * rvb->coeff[j][3]) -
				(rvb->l_buf_flt_out[j][1] * rvb->coeff[j][4])) / 1024;
			rvb->l_buf_flt_in[j][1] = rvb->l_buf_flt_in[j][0];
			rvb->l_buf_flt_in[j][0] = l_rfl;
			rvb->l_buf_flt_out[j][1] = rvb->l_buf_flt_out[j][0];
			rvb->l_buf_flt_out[j][0] = l_buf_flt;
			buffer[i] += l_buf_flt;
		
			r_buf_flt = ((r_rfl * rvb->coeff[j][0]) +
				(rvb->r_buf_flt_in[j][0] * rvb->coeff[j][1]) +
				(rvb->r_buf_flt_in[j][1] * rvb->coeff[j][2]) -
				(rvb->r_buf_flt_out[j][0] * rvb->coeff[j][3]) -
				(rvb->r_buf_flt_out[j][1] * rvb->coeff[j][4])) / 1024;
			rvb->r_buf_flt_in[j][1] = rvb->r_buf_flt_in[j][0];
			rvb->r_buf_flt_in[j][0] = r_rfl;
			rvb->r_buf_flt_out[j][1] = rvb->r_buf_flt_out[j][0];
			rvb->r_buf_flt_out[j][0] = r_buf_flt;
			buffer[i+1] += r_buf_flt;
		}
		
		/*
			add filtered result back into the buffers but on the opposite side
		*/
		tmp_l_val = buffer[i+1] / vol_div;
		tmp_r_val = buffer[i] / vol_div;
		for (j = 0; j < 4; j++) {
			rvb->l_buf[rvb->l_in[j]] += tmp_l_val;
			rvb->l_in[j] = (rvb->l_in[j] + 1) % rvb->l_buf_size;
			
			rvb->r_buf[rvb->r_in[j]] += tmp_r_val;
			rvb->r_in[j] = (rvb->r_in[j] + 1) % rvb->r_buf_size;
		}
	}
	return;
}

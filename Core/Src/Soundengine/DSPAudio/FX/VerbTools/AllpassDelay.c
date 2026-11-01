/*
  ==============================================================================

    AllpassDelay.cpp
    Created: 21 Jul 2014 7:29:52pm
    Author:  julian

  ==============================================================================
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
*/

#include "Soundengine/DSPAudio/FX/VerbTools/AllpassDelay.h"

void AllpassDelay_init(AllpassDelay *d)
{
    d->g = INITIAL_ALLPASS_COFF;
    d->out=d->in=0;
    VerbDelay_init(&d->delay);
}
float AllpassDelay_process(AllpassDelay *d, float input)
{
    const float out = VerbDelay_read(&d->delay);
    const float in = input + d->g*out;
    VerbDelay_write(&d->delay, in);
    return out + (-d->g * in);
}
void AllpassDelay_setCoefficient(AllpassDelay *d, float coff)
{
	   d->g = coff;
}
void AllpassDelay_setDelay(AllpassDelay *d, float del)
{
	VerbDelay_setLength(&d->delay , del);
}
void AllpassDelay_setBuffer(AllpassDelay *d, volatile __fp16 *buffer)
{
	  VerbDelay_setBuffer(&d->delay,  buffer);
}
void AllpassDelay_setRoomSize(AllpassDelay *d, float sz)
{
   VerbDelay_setRoomSize(&d->delay, sz);
}



//----------------------------------------------------------------------

void AllpassModDelay_init(AllpassModDelay *d)
{
    d->g = INITIAL_ALLPASS_COFF;
    d->out=d->in=0;
    VerbModDelay_init(&d->delay);
}
float AllpassModDelay_process(AllpassModDelay *d, float input)
{
    /* Between two samples, as Reverb_process moves the distance. VerbModDelay_calcLength would leave both allpasses at the whole 2048
     * samples: its filter's coefficient rounds to 0 in single precision, and their equal lengths flutter */
    const uint32_t at = (d->delay.writePtr - d->delay.distance) & (MAX_ALLPASS_SIZE - 1);
    const float far = (float)d->delay.ramBuffer[at];
    const float out = far + d->delay.frac * ((float)d->delay.ramBuffer[(at + 1) & (MAX_ALLPASS_SIZE - 1)] - far);
    const float in = input + d->g*out;
    VerbModDelay_write(&d->delay, in);
    return out + (-d->g * in);
}
void AllpassModDelay_setCoefficient(AllpassModDelay *d, float coff)
{
	d->g = coff;
}
void AllpassModDelay_setDelay(AllpassModDelay *d, float del)
{
	VerbModDelay_setLength(&d->delay , del);
}
void AllpassModDelay_setBuffer(AllpassModDelay *d, volatile __fp16 *buffer)
{
	VerbModDelay_setBuffer(&d->delay,  buffer);
}
void AllpassModDelay_setRoomSize(AllpassModDelay *d, float sz)
{
	 VerbModDelay_setRoomSize(&d->delay, sz);
}
void AllpassModDelay_setModDepth(AllpassModDelay *d,float depth)
{
  VerbModDelay_setModDepth(&d->delay, depth);
}

void AllpassModDelay_setModValue(AllpassModDelay *d,float v)
{
	VerbModDelay_setModValue(&d->delay, v);
	VerbModDelay_calcLength(&d->delay);
}

#if 0


//########################################
  template<typename T>
  ApDelay<T>::ApDelay()
  {
    g = INITIAL_ALLPASS_COFF;
    out=in=0;
  }
  
  template<typename T>
  ApDelay<T>::~ApDelay()
  {
  }
  template<typename T>
  void ApDelay<T>::setCoefficient(float coff)
  {
    g = coff;
  }
  
//#############################################
  template<typename T>
  T AllpassDelay<T>::process(const T input)
  {
      this->out = delay.read();
      this->in = input + this->g*this->out;
      delay.write(this->in);
      return this->out + -this->g * this->in;
  }
  
  template<typename T>
  void AllpassDelay<T>::setDelay(float d)
  {
    delay.setLength(d);
  }
//###############################################
  template<typename T>
  T AllpassModDelay<T>::process(const T input)
  {
      delay.calcLength();
      this->out = delay.read();
      this->in = input + this->g*this->out;
      delay.write(this->in);
      return this->out + -this->g * this->in;
  }

  template<typename T>
  void AllpassModDelay<T>::setDelay(float d)
  {
    delay.setLength(d);
  }

//###############################################
  
  // Explicit template instantiation
template class AllpassDelay<revItg>;
template class AllpassModDelay<revItg>;
template class ApDelay<revItg>;

#endif

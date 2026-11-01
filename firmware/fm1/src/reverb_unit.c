/* SPDX-License-Identifier: GPL-3.0-only */
/* The reverb and its parts as one file to the compiler. Apart they make 46 calls a sample, and together they inline to one loop with the
 * same arithmetic, which is about a third of what the reverb costs. fm1.toml lists this in place of the files it includes. */
#include <math.h>
#include "Soundengine/DSPAudio/1PoleLp.h"

static inline float reverb_one_pole(OnePoleFilter *filter, float input)
{
    filter->b = fmaf(filter->r, input - filter->b, filter->b);
    return filter->b;
}
#define calcOnePole reverb_one_pole

#include "../../../Core/Src/Soundengine/DSPAudio/FX/VerbTools/VerbDelay.c"
#include "../../../Core/Src/Soundengine/DSPAudio/FX/VerbTools/VerbModDelay.c"
#include "../../../Core/Src/Soundengine/DSPAudio/FX/VerbTools/AllpassDelay.c"
#include "../../../Core/Src/Soundengine/DSPAudio/FX/VerbTools/sinCos.c"
#include "../../../Core/Src/Soundengine/DSPAudio/TiltEq.c"
#include "../../../Core/Src/Soundengine/DSPAudio/FX/Reverb.c"

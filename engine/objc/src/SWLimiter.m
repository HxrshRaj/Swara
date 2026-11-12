#import "SWLimiter.h"
#include <stdlib.h>
#include "swara_limiter.h"

@implementation SWLimiter

- (instancetype)initWithThreshold:(float)threshold
                         attackMs:(float)attackMs
                        releaseMs:(float)releaseMs
                       sampleRate:(float)sampleRate {
    self = [super init];
    if (self == nil) return nil;
    _lim = (struct swara_limiter *)calloc(1, sizeof(struct swara_limiter));
    if (_lim == NULL || swara_limiter_init(_lim, threshold, attackMs, releaseMs, sampleRate) != SWARA_OK) {
        [self release];
        return nil;
    }
    return self;
}

- (void)dealloc {
    free(_lim);
    [super dealloc];
}

- (float)threshold { return _lim->threshold; }
- (float)currentGain { return _lim->gain; }
- (void)reset { swara_limiter_reset(_lim); }

- (NSData *)processSamples:(NSData *)samples {
    if (samples == nil || ([samples length] % sizeof(float)) != 0) return nil;
    NSUInteger n = [samples length] / sizeof(float);
    NSMutableData *out = [NSMutableData dataWithLength:[samples length]];
    if (n > 0 && swara_limiter_process(_lim, (const float *)[samples bytes], (float *)[out mutableBytes], n) != SWARA_OK)
        return nil;
    return out;
}

@end

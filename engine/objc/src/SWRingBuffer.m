#import "SWRingBuffer.h"
#include <stdlib.h>
#include "swara_ring.h"

NSString *const SWErrorDomain = @"SwaraErrorDomain";

static NSError *SWMakeError(SWErrorCode code, NSString *message) {
    return [NSError errorWithDomain:SWErrorDomain
                               code:code
                           userInfo:[NSDictionary dictionaryWithObject:message forKey:NSLocalizedDescriptionKey]];
}

@implementation SWRingBuffer

- (instancetype)initWithCapacity:(NSUInteger)capacity {
    self = [super init];
    if (self == nil) return nil;
    _storage = [[NSMutableData alloc] initWithLength:capacity * sizeof(float)];
    _ring = (swara_ring *)calloc(1, swara_ring_sizeof());
    if (_ring == NULL || swara_ring_init(_ring, (float *)[_storage mutableBytes], capacity) != SWARA_OK) {
        [self release];
        return nil;
    }
    return self;
}

- (void)dealloc {
    free(_ring);
    [_storage release];
    [super dealloc];
}

- (NSUInteger)availableSamples { return swara_ring_available(_ring); }
- (NSUInteger)freeSamples { return swara_ring_space(_ring); }

- (BOOL)writeSamples:(NSData *)samples error:(NSError **)error {
    if (samples == nil || ([samples length] % sizeof(float)) != 0) {
        if (error) *error = SWMakeError(SWErrorInvalidArgument, @"samples must be a whole number of 32-bit floats");
        return NO;
    }
    swara_status s = swara_ring_write(_ring, (const float *)[samples bytes], [samples length] / sizeof(float));
    if (s == SWARA_OK) return YES;
    if (error) {
        *error = (s == SWARA_ERR_FULL) ? SWMakeError(SWErrorFull, @"not enough free space")
                                       : SWMakeError(SWErrorInvalidArgument, @"invalid argument");
    }
    return NO;
}

- (NSData *)readSamples:(NSUInteger)count error:(NSError **)error {
    NSMutableData *out = [NSMutableData dataWithLength:count * sizeof(float)];
    swara_status s = swara_ring_read(_ring, (float *)[out mutableBytes], count);
    if (s == SWARA_OK) return out;
    if (error) {
        *error = (s == SWARA_ERR_EMPTY) ? SWMakeError(SWErrorEmpty, @"not enough samples available")
                                        : SWMakeError(SWErrorInvalidArgument, @"invalid argument");
    }
    return nil;
}

@end

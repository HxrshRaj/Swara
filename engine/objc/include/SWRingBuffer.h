// SWRingBuffer: Objective-C wrapper around the C11 lock-free swara_ring.
// Single producer thread calls -writeSamples:error:, single consumer thread calls -readSamples:error:.
#import <Foundation/Foundation.h>

extern NSString *const SWErrorDomain;

typedef NS_ENUM(NSInteger, SWErrorCode) {
    SWErrorInvalidArgument = 1,
    SWErrorFull = 2,
    SWErrorEmpty = 3
};

struct swara_ring;

@interface SWRingBuffer : NSObject {
    struct swara_ring *_ring;   // heap-allocated C11 ring (kept opaque here)
    NSMutableData *_storage;
}

/// capacity must be a power of two, at least 2. Usable size is capacity - 1. Returns nil otherwise.
- (instancetype)initWithCapacity:(NSUInteger)capacity;

@property(nonatomic, readonly) NSUInteger availableSamples;  // readable now
@property(nonatomic, readonly) NSUInteger freeSamples;       // writable now

/// All-or-nothing. `samples` holds 32-bit floats. Returns NO (and sets error) if there is not enough space.
- (BOOL)writeSamples:(NSData *)samples error:(NSError **)error;

/// All-or-nothing read of `count` floats. Returns nil (and sets error) if fewer are available.
- (NSData *)readSamples:(NSUInteger)count error:(NSError **)error;

@end

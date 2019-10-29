//
//  FBMainThread.m
//  Emulator
//
//  Created by Akop Karapetyan on 10/16/19.
//  Copyright © 2019 Akop Karapetyan. All rights reserved.
//

#import "FBMainThread.h"

#include "burner.h"

// Placing burner.h stuff in the NSThread file
// messes with thread execution for some reason,
// so I'm implementing it as a category - AK

@implementation FBMainThread (Etc)

- (BOOL) isPaused
{
    return bRunPause;
}

- (void) setPaused:(BOOL) isPaused
{
    if (!bDrvOkay || bRunPause == isPaused)
        return;

    bRunPause = isPaused;
    if (isPaused)
        AudSoundStop();
    else
        AudSoundPlay();
}

- (NSString *) setName
{
    return [NSString stringWithCString:BurnDrvGetText(DRV_NAME)
                              encoding:NSUTF8StringEncoding];
}

- (NSString *) title
{
    return [NSString stringWithCString:BurnDrvGetText(DRV_FULLNAME)
                              encoding:NSUTF8StringEncoding];
}

#pragma mark - DIP switches

- (NSArray<FBDipSetting *> *) dipSwitches
{
    if (!bDrvOkay)
        return nil;

    NSMutableArray *switches = [NSMutableArray new];
    FBDipSetting *active;

    int offset = 0;
    BurnDIPInfo dipSwitch;
    for (int i = 0; BurnDrvGetDIPInfo(&dipSwitch, i) == 0; i++) {
        if (!dipSwitch.szText) // defaruto
            continue;

        if (dipSwitch.nFlags == 0xf0)
            offset = dipSwitch.nInput;
        if (dipSwitch.nFlags & 0x40) {
            FBDipSetting *set = [FBDipSetting new];
            set.name = [NSString stringWithCString:dipSwitch.szText
                                          encoding:NSUTF8StringEncoding];

            active = set;
            set.selectedIndex = 0;
            set.switches = [NSMutableArray new];
            [switches addObject:set];
        } else {
            FBDipOption *opt = [FBDipOption new];
            opt.name = [NSString stringWithCString:dipSwitch.szText
                                          encoding:NSUTF8StringEncoding];
            opt.start = dipSwitch.nInput + offset;
            opt.mask = dipSwitch.nMask;
            opt.setting = dipSwitch.nSetting;

            // Default switch
            BurnDIPInfo dsw2;
            for (int j = 0; BurnDrvGetDIPInfo(&dsw2, j) == 0; j++)
                if (dsw2.nFlags == 0xff && dsw2.nInput == dipSwitch.nInput
                    && (dsw2.nSetting & dipSwitch.nMask) == dipSwitch.nSetting)
                        active.defaultIndex = active.selectedIndex = active.switches.count;

            // Active switch
            struct GameInp *pgi = GameInp + opt.start;
            if ((pgi->Input.Constant.nConst & opt.mask) == dipSwitch.nSetting)
                active.selectedIndex = active.switches.count;

            [(NSMutableArray *) active.switches addObject:opt];
        }
    }

    return switches;
}

- (void) applyDip:(FBDipOption *) option
{
    if (bDrvOkay) {
        struct GameInp *pgi = GameInp + option.start;
        pgi->Input.Constant.nConst = (pgi->Input.Constant.nConst & ~option.mask) | (option.setting & option.mask);
        self.dipSwitchesDirty = YES;
    }
}

- (BOOL) saveDipState:(NSString *) path
{
    NSLog(@"saveDipState");

    NSArray<FBDipSetting *> *switches = self.dipSwitches;
    if (!switches || switches.count < 1)
        return YES;

    for (FBDipSetting *sw in switches)
        if (sw.defaultIndex != sw.selectedIndex)
            goto save;

    return YES;

save:
    FILE *f = fopen([path cStringUsingEncoding:NSUTF8StringEncoding], "w");
    if (!f)
        return NO;

    for (FBDipSetting *sw in switches) {
        FBDipOption *opt = sw.switches[sw.selectedIndex];
        fprintf(f, "%x %d %02x\n", opt.start, sw.selectedIndex, opt.setting);
    }

    fclose(f);
    return YES;
}

- (BOOL) restoreDipState:(NSString *) path
{
    NSLog(@"restoreDipState");

    NSArray<FBDipSetting *> *switches = self.dipSwitches;
    if (!switches || switches.count < 1)
        return YES;

    FILE *f = fopen([path cStringUsingEncoding:NSUTF8StringEncoding], "r");
    if (!f)
        return NO;

    int swIndex;
    uint32 start;
    uint32 setting;
    for (int i = 0; fscanf(f, "%x %d %x", &start, &swIndex, &setting) == 3 && i < switches.count; i++) {
        FBDipSetting *sw = switches[i];
        if (sw.selectedIndex != swIndex && swIndex < sw.switches.count)
            [self applyDip:sw.switches[swIndex]];
    }

    fclose(f);
    return YES;
}

@end

#pragma mark - FBDipSetting

@implementation FBDipSetting

- (instancetype) init
{
    if (self = [super init]) {
        _switches = [NSMutableArray new];
    }
    return self;
}

@end

#pragma mark - FBDipOption

@implementation FBDipOption

@end
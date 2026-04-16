#import <Cocoa/Cocoa.h>

@class StateMachineGraphView;

@protocol StateMachineGraphViewDelegate <NSObject>
@optional
- (void)graphView:(StateMachineGraphView*)view didSelectState:(int)stateIndex;
- (void)graphView:(StateMachineGraphView*)view
    didSelectTransition:(int)stateIndex
        transitionIndex:(int)transIndex;
- (void)graphView:(StateMachineGraphView*)view didForceSetState:(int)stateIndex;
- (void)graphViewDidRequestAddState:(StateMachineGraphView*)view;
- (void)graphView:(StateMachineGraphView*)view didRequestRemoveState:(int)stateIndex;
@end

@interface StateMachineGraphView : NSView

@property(nonatomic, unsafe_unretained) id<StateMachineGraphViewDelegate> delegate;

- (void)setStateCount:(int)count
                names:(NSArray<NSString*>*)names
            clipNames:(NSArray<NSString*>*)clipNames
         currentState:(int)current
        selectedState:(int)selected
   selectedTransition:(int)selTrans;

- (void)setTransitions:(NSArray<NSNumber*>*)from
                    to:(NSArray<NSNumber*>*)to
                labels:(NSArray<NSString*>*)labels
       stateTransIdxes:(NSArray<NSNumber*>*)stateTransIdxes;

@end

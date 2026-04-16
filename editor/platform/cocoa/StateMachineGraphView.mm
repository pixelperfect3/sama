#import "editor/platform/cocoa/StateMachineGraphView.h"

static const CGFloat kNodeWidth = 120.0;
static const CGFloat kNodeHeight = 50.0;
static const CGFloat kNodeSpacingX = 200.0;
static const CGFloat kNodeSpacingY = 100.0;
static const CGFloat kNodeCornerRadius = 8.0;
static const CGFloat kArrowHeadSize = 8.0;
static const CGFloat kMinZoom = 0.5;
static const CGFloat kMaxZoom = 2.0;

// Storage object for all mutable state — avoids ARC issues with ivars
// in NSView subclass @implementation blocks compiled as ObjC++.
@interface SMGraphStorage : NSObject
@property(nonatomic, strong) NSMutableArray<NSValue*>* nodePositions;
@property(nonatomic, strong) NSMutableArray<NSString*>* nodeNames;
@property(nonatomic, strong) NSMutableArray<NSString*>* nodeClipNames;
@property(nonatomic, strong) NSMutableArray<NSNumber*>* transFromState;
@property(nonatomic, strong) NSMutableArray<NSNumber*>* transToState;
@property(nonatomic, strong) NSMutableArray<NSString*>* transCondLabel;
@property(nonatomic, strong) NSMutableArray<NSNumber*>* transStateTransIdx;
@property(nonatomic, assign) int numStates;
@property(nonatomic, assign) int currentStateIndex;
@property(nonatomic, assign) int selectedStateIndex;
@property(nonatomic, assign) int selectedTransitionIndex;
@property(nonatomic, assign) CGFloat zoomLevel;
@property(nonatomic, assign) NSPoint panOffset;
@property(nonatomic, assign) BOOL isDraggingNode;
@property(nonatomic, assign) int dragNodeIndex;
@property(nonatomic, assign) NSPoint dragStartMouse;
@property(nonatomic, assign) NSPoint dragStartNodePos;
@property(nonatomic, assign) BOOL isPanning;
@property(nonatomic, assign) NSPoint panStartMouse;
@property(nonatomic, assign) NSPoint panStartOffset;
@property(nonatomic, assign) BOOL positionsInitialized;
@end

@implementation SMGraphStorage
@end

@implementation StateMachineGraphView
{
    SMGraphStorage* _s;
    void* _graphDelegateRaw;
}

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        _s = [[SMGraphStorage alloc] init];
        _s.nodePositions = [NSMutableArray array];
        _s.nodeNames = [NSMutableArray array];
        _s.nodeClipNames = [NSMutableArray array];
        _s.transFromState = [NSMutableArray array];
        _s.transToState = [NSMutableArray array];
        _s.transCondLabel = [NSMutableArray array];
        _s.transStateTransIdx = [NSMutableArray array];
        _s.currentStateIndex = -1;
        _s.selectedStateIndex = -1;
        _s.selectedTransitionIndex = -1;
        _s.numStates = 0;
        _s.zoomLevel = 1.0;
        _s.panOffset = NSMakePoint(10.0, 10.0);
        _s.isDraggingNode = NO;
        _s.dragNodeIndex = -1;
        _s.isPanning = NO;
        _s.positionsInitialized = NO;
    }
    return self;
}

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)setGraphDelegate:(id<StateMachineGraphViewDelegate>)delegate
{
    _graphDelegateRaw = (__bridge void*)delegate;
}

- (id<StateMachineGraphViewDelegate>)graphDelegate
{
    return (__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw;
}

- (void)updateWithStateCount:(int)count
                names:(NSArray<NSString*>*)names
            clipNames:(NSArray<NSString*>*)clipNames
         currentState:(int)current
        selectedState:(int)selected
   selectedTransition:(int)selTrans
{
    BOOL countChanged = (count != _s.numStates);
    _s.numStates = count;
    _s.currentStateIndex = current;
    _s.selectedStateIndex = selected;
    _s.selectedTransitionIndex = selTrans;

    [_s.nodeNames removeAllObjects];
    if (names)
        [_s.nodeNames addObjectsFromArray:names];
    [_s.nodeClipNames removeAllObjects];
    if (clipNames)
        [_s.nodeClipNames addObjectsFromArray:clipNames];

    // Auto-layout: arrange in a grid when positions need initialization.
    if (countChanged || !_s.positionsInitialized || (int)_s.nodePositions.count != count)
    {
        [_s.nodePositions removeAllObjects];
        int cols = MAX(1, (int)ceil(sqrt((double)count)));
        for (int i = 0; i < count; ++i)
        {
            int col = i % cols;
            int row = i / cols;
            CGFloat x = 20.0 + col * kNodeSpacingX;
            CGFloat y = 20.0 + row * kNodeSpacingY;
            [_s.nodePositions addObject:[NSValue valueWithPoint:NSMakePoint(x, y)]];
        }
        _s.positionsInitialized = (count > 0);
    }

    [self setNeedsDisplay:YES];
}

- (void)setTransitions:(NSArray<NSNumber*>*)from
                    to:(NSArray<NSNumber*>*)to
                labels:(NSArray<NSString*>*)labels
       stateTransIdxes:(NSArray<NSNumber*>*)stateTransIdxes
{
    if (!_s.transFromState)
        _s.transFromState = [NSMutableArray array];
    if (!_s.transToState)
        _s.transToState = [NSMutableArray array];
    if (!_s.transCondLabel)
        _s.transCondLabel = [NSMutableArray array];
    if (!_s.transStateTransIdx)
        _s.transStateTransIdx = [NSMutableArray array];

    [_s.transFromState removeAllObjects];
    if (from)
        [_s.transFromState addObjectsFromArray:from];
    [_s.transToState removeAllObjects];
    if (to)
        [_s.transToState addObjectsFromArray:to];
    [_s.transCondLabel removeAllObjects];
    if (labels)
        [_s.transCondLabel addObjectsFromArray:labels];
    [_s.transStateTransIdx removeAllObjects];
    if (stateTransIdxes)
        [_s.transStateTransIdx addObjectsFromArray:stateTransIdxes];
    [self setNeedsDisplay:YES];
}

- (NSPoint)nodeCenterForIndex:(int)idx
{
    if (idx < 0 || idx >= (int)_s.nodePositions.count)
        return NSZeroPoint;
    NSPoint p = _s.nodePositions[idx].pointValue;
    return NSMakePoint(p.x + kNodeWidth / 2.0, p.y + kNodeHeight / 2.0);
}

- (NSRect)nodeRectForIndex:(int)idx
{
    if (idx < 0 || idx >= (int)_s.nodePositions.count)
        return NSZeroRect;
    NSPoint p = _s.nodePositions[idx].pointValue;
    return NSMakeRect(p.x, p.y, kNodeWidth, kNodeHeight);
}

// Convert a point in view coords to canvas coords (accounting for pan/zoom).
- (NSPoint)viewToCanvas:(NSPoint)viewPt
{
    return NSMakePoint((viewPt.x - _s.panOffset.x) / _s.zoomLevel,
                       (viewPt.y - _s.panOffset.y) / _s.zoomLevel);
}

- (void)drawRect:(NSRect)dirtyRect
{
    [super drawRect:dirtyRect];

    NSRect bounds = self.bounds;

    // Background.
    [[NSColor windowBackgroundColor] setFill];
    NSRectFill(bounds);

    // Draw a subtle border.
    [[NSColor separatorColor] setStroke];
    NSBezierPath* borderPath = [NSBezierPath bezierPathWithRect:bounds];
    borderPath.lineWidth = 1.0;
    [borderPath stroke];

    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGContextSaveGState(ctx);

    // Apply pan and zoom.
    CGContextTranslateCTM(ctx, _s.panOffset.x, _s.panOffset.y);
    CGContextScaleCTM(ctx, _s.zoomLevel, _s.zoomLevel);

    // Draw transitions (behind nodes).
    for (NSUInteger ti = 0; ti < _s.transFromState.count; ++ti)
    {
        int fromIdx = _s.transFromState[ti].intValue;
        int toIdx = _s.transToState[ti].intValue;
        if (fromIdx < 0 || fromIdx >= _s.numStates || toIdx < 0 || toIdx >= _s.numStates)
            continue;

        NSPoint fromCenter = [self nodeCenterForIndex:fromIdx];
        NSPoint toCenter = [self nodeCenterForIndex:toIdx];

        // Determine if this transition is selected.
        int combined = _s.transStateTransIdx[ti].intValue;
        int tStateIdx = combined / 1000;
        int tTransIdx = combined % 1000;
        BOOL isSelected =
            (tStateIdx == _s.selectedStateIndex && tTransIdx == _s.selectedTransitionIndex);

        // Compute start/end on node edges.
        NSPoint startPt, endPt;
        if (fromIdx == toIdx)
        {
            // Self-transition: draw a loop above the node.
            NSRect nodeRect = [self nodeRectForIndex:fromIdx];
            startPt = NSMakePoint(nodeRect.origin.x + kNodeWidth * 0.3, nodeRect.origin.y);
            endPt = NSMakePoint(nodeRect.origin.x + kNodeWidth * 0.7, nodeRect.origin.y);

            NSBezierPath* loopPath = [NSBezierPath bezierPath];
            [loopPath moveToPoint:startPt];
            NSPoint cp1 = NSMakePoint(startPt.x - 20.0, startPt.y - 40.0);
            NSPoint cp2 = NSMakePoint(endPt.x + 20.0, endPt.y - 40.0);
            [loopPath curveToPoint:endPt controlPoint1:cp1 controlPoint2:cp2];

            if (isSelected)
                [[NSColor systemBlueColor] setStroke];
            else
                [[NSColor systemGrayColor] setStroke];
            loopPath.lineWidth = 1.5 / _s.zoomLevel;
            [loopPath stroke];

            // Arrowhead at end.
            [self drawArrowHeadAt:endPt
                        direction:NSMakePoint(endPt.x - cp2.x, endPt.y - cp2.y)
                       isSelected:isSelected];

            // Label at top of loop.
            if (ti < _s.transCondLabel.count && _s.transCondLabel[ti].length > 0)
            {
                NSPoint labelPt = NSMakePoint((startPt.x + endPt.x) / 2.0, startPt.y - 45.0);
                [self drawLabel:_s.transCondLabel[ti] atPoint:labelPt isSelected:isSelected];
            }
            continue;
        }

        // Offset for multiple arrows between same pair: nudge perpendicular.
        CGFloat dx = toCenter.x - fromCenter.x;
        CGFloat dy = toCenter.y - fromCenter.y;
        CGFloat dist = sqrt(dx * dx + dy * dy);
        if (dist < 1.0)
            dist = 1.0;
        CGFloat nx = dx / dist;
        CGFloat ny = dy / dist;

        // Start from edge of source node, end at edge of target node.
        startPt = NSMakePoint(fromCenter.x + nx * kNodeWidth / 2.0,
                              fromCenter.y + ny * kNodeHeight / 2.0);
        endPt =
            NSMakePoint(toCenter.x - nx * kNodeWidth / 2.0, toCenter.y - ny * kNodeHeight / 2.0);

        // Control points for gentle bezier curve: offset perpendicular.
        CGFloat perpX = -ny * 20.0;
        CGFloat perpY = nx * 20.0;
        NSPoint cp1 = NSMakePoint((startPt.x + endPt.x) / 2.0 + perpX,
                                  (startPt.y + endPt.y) / 2.0 + perpY);

        NSBezierPath* arrowPath = [NSBezierPath bezierPath];
        [arrowPath moveToPoint:startPt];
        [arrowPath curveToPoint:endPt controlPoint1:cp1 controlPoint2:cp1];

        if (isSelected)
            [[NSColor systemBlueColor] setStroke];
        else
            [[NSColor systemGrayColor] setStroke];
        arrowPath.lineWidth = 1.5 / _s.zoomLevel;
        [arrowPath stroke];

        // Arrowhead at end.
        CGFloat adx = endPt.x - cp1.x;
        CGFloat ady = endPt.y - cp1.y;
        [self drawArrowHeadAt:endPt direction:NSMakePoint(adx, ady) isSelected:isSelected];

        // Label at midpoint.
        if (ti < _s.transCondLabel.count && _s.transCondLabel[ti].length > 0)
        {
            // Bezier midpoint approximation (t=0.5 with quadratic).
            NSPoint mid = NSMakePoint(0.25 * startPt.x + 0.5 * cp1.x + 0.25 * endPt.x,
                                      0.25 * startPt.y + 0.5 * cp1.y + 0.25 * endPt.y);
            [self drawLabel:_s.transCondLabel[ti] atPoint:mid isSelected:isSelected];
        }
    }

    // Draw state nodes.
    for (int i = 0; i < _s.numStates; ++i)
    {
        NSRect nodeRect = [self nodeRectForIndex:i];

        // Fill.
        NSColor* fillColor = [NSColor controlBackgroundColor];
        [fillColor setFill];
        NSBezierPath* nodePath = [NSBezierPath bezierPathWithRoundedRect:nodeRect
                                                                 xRadius:kNodeCornerRadius
                                                                 yRadius:kNodeCornerRadius];
        [nodePath fill];

        // Border.
        NSColor* borderColor;
        CGFloat borderWidth = 1.5 / _s.zoomLevel;
        if (i == _s.currentStateIndex && i == _s.selectedStateIndex)
        {
            // Both active and selected: use green with blue inner glow.
            borderColor = [NSColor systemGreenColor];
            borderWidth = 2.5 / _s.zoomLevel;
        }
        else if (i == _s.currentStateIndex)
        {
            borderColor = [NSColor systemGreenColor];
            borderWidth = 2.0 / _s.zoomLevel;
        }
        else if (i == _s.selectedStateIndex)
        {
            borderColor = [NSColor systemBlueColor];
            borderWidth = 2.0 / _s.zoomLevel;
        }
        else
        {
            borderColor = [NSColor separatorColor];
        }

        [borderColor setStroke];
        nodePath.lineWidth = borderWidth;
        [nodePath stroke];

        // State name (bold, centered).
        NSString* name = (i < (int)_s.nodeNames.count) ? _s.nodeNames[i] : @"?";
        NSString* clipName = (i < (int)_s.nodeClipNames.count) ? _s.nodeClipNames[i] : @"";

        NSDictionary* nameAttrs = @{
            NSFontAttributeName : [NSFont boldSystemFontOfSize:10.0],
            NSForegroundColorAttributeName : [NSColor labelColor]
        };
        NSSize nameSize = [name sizeWithAttributes:nameAttrs];
        NSPoint namePos = NSMakePoint(nodeRect.origin.x + (kNodeWidth - nameSize.width) / 2.0,
                                      nodeRect.origin.y + 8.0);
        [name drawAtPoint:namePos withAttributes:nameAttrs];

        // Clip name (smaller, below state name).
        if (clipName.length > 0)
        {
            NSDictionary* clipAttrs = @{
                NSFontAttributeName : [NSFont systemFontOfSize:9.0],
                NSForegroundColorAttributeName : [NSColor secondaryLabelColor]
            };
            NSSize clipSize = [clipName sizeWithAttributes:clipAttrs];
            NSPoint clipPos = NSMakePoint(nodeRect.origin.x + (kNodeWidth - clipSize.width) / 2.0,
                                          nodeRect.origin.y + 26.0);
            [clipName drawAtPoint:clipPos withAttributes:clipAttrs];
        }
    }

    CGContextRestoreGState(ctx);

    // Draw "State Graph" label in top-left corner (not affected by zoom/pan).
    NSDictionary* headerAttrs = @{
        NSFontAttributeName : [NSFont boldSystemFontOfSize:9.0],
        NSForegroundColorAttributeName : [NSColor tertiaryLabelColor]
    };
    [@"State Graph" drawAtPoint:NSMakePoint(4.0, 4.0) withAttributes:headerAttrs];
}

- (void)drawArrowHeadAt:(NSPoint)tip direction:(NSPoint)dir isSelected:(BOOL)selected
{
    CGFloat len = sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 0.001)
        return;
    CGFloat nx = dir.x / len;
    CGFloat ny = dir.y / len;

    CGFloat sz = kArrowHeadSize / _s.zoomLevel;
    NSPoint base1 = NSMakePoint(tip.x - nx * sz + ny * sz * 0.4, tip.y - ny * sz - nx * sz * 0.4);
    NSPoint base2 = NSMakePoint(tip.x - nx * sz - ny * sz * 0.4, tip.y - ny * sz + nx * sz * 0.4);

    NSBezierPath* head = [NSBezierPath bezierPath];
    [head moveToPoint:tip];
    [head lineToPoint:base1];
    [head lineToPoint:base2];
    [head closePath];

    if (selected)
        [[NSColor systemBlueColor] setFill];
    else
        [[NSColor systemGrayColor] setFill];
    [head fill];
}

- (void)drawLabel:(NSString*)label atPoint:(NSPoint)pt isSelected:(BOOL)selected
{
    NSDictionary* attrs = @{
        NSFontAttributeName : [NSFont systemFontOfSize:8.0],
        NSForegroundColorAttributeName : selected ? [NSColor systemBlueColor]
                                                  : [NSColor secondaryLabelColor]
    };

    NSSize labelSize = [label sizeWithAttributes:attrs];

    // Draw a small background pill behind the text for readability.
    NSRect bgRect =
        NSMakeRect(pt.x - labelSize.width / 2.0 - 3.0, pt.y - labelSize.height / 2.0 - 1.0,
                   labelSize.width + 6.0, labelSize.height + 2.0);
    [[NSColor windowBackgroundColor] setFill];
    NSBezierPath* bg = [NSBezierPath bezierPathWithRoundedRect:bgRect xRadius:3.0 yRadius:3.0];
    [bg fill];

    NSPoint drawPt = NSMakePoint(pt.x - labelSize.width / 2.0, pt.y - labelSize.height / 2.0);
    [label drawAtPoint:drawPt withAttributes:attrs];
}

// --- Hit testing ---

- (int)hitTestNodeAtPoint:(NSPoint)canvasPt
{
    for (int i = _s.numStates - 1; i >= 0; --i)
    {
        NSRect r = [self nodeRectForIndex:i];
        if (NSPointInRect(canvasPt, r))
            return i;
    }
    return -1;
}

- (int)hitTestTransitionAtPoint:(NSPoint)canvasPt
{
    CGFloat threshold = 8.0 / _s.zoomLevel;
    for (NSUInteger ti = 0; ti < _s.transFromState.count; ++ti)
    {
        int fromIdx = _s.transFromState[ti].intValue;
        int toIdx = _s.transToState[ti].intValue;
        if (fromIdx < 0 || fromIdx >= _s.numStates || toIdx < 0 || toIdx >= _s.numStates)
            continue;
        if (fromIdx == toIdx)
            continue; // Skip self-loops for simplicity.

        NSPoint fromCenter = [self nodeCenterForIndex:fromIdx];
        NSPoint toCenter = [self nodeCenterForIndex:toIdx];

        CGFloat dx = toCenter.x - fromCenter.x;
        CGFloat dy = toCenter.y - fromCenter.y;
        CGFloat dist = sqrt(dx * dx + dy * dy);
        if (dist < 1.0)
            continue;
        CGFloat nx = dx / dist;
        CGFloat ny = dy / dist;

        NSPoint startPt = NSMakePoint(fromCenter.x + nx * kNodeWidth / 2.0,
                                      fromCenter.y + ny * kNodeHeight / 2.0);
        NSPoint endPt =
            NSMakePoint(toCenter.x - nx * kNodeWidth / 2.0, toCenter.y - ny * kNodeHeight / 2.0);

        CGFloat perpX = -ny * 20.0;
        CGFloat perpY = nx * 20.0;
        NSPoint cp = NSMakePoint((startPt.x + endPt.x) / 2.0 + perpX,
                                 (startPt.y + endPt.y) / 2.0 + perpY);

        // Sample the quadratic bezier and check distance.
        for (int s = 0; s <= 20; ++s)
        {
            CGFloat t = (CGFloat)s / 20.0;
            CGFloat invT = 1.0 - t;
            CGFloat bx = invT * invT * startPt.x + 2.0 * invT * t * cp.x + t * t * endPt.x;
            CGFloat by = invT * invT * startPt.y + 2.0 * invT * t * cp.y + t * t * endPt.y;
            CGFloat ddx = canvasPt.x - bx;
            CGFloat ddy = canvasPt.y - by;
            if (ddx * ddx + ddy * ddy < threshold * threshold)
                return (int)ti;
        }
    }
    return -1;
}

// --- Mouse events ---

- (void)mouseDown:(NSEvent*)event
{
    NSPoint viewPt = [self convertPoint:event.locationInWindow fromView:nil];
    NSPoint canvasPt = [self viewToCanvas:viewPt];

    // Check for option-drag (pan).
    if (event.modifierFlags & NSEventModifierFlagOption)
    {
        _s.isPanning = YES;
        _s.panStartMouse = viewPt;
        _s.panStartOffset = _s.panOffset;
        return;
    }

    // Hit test nodes first.
    int hitNode = [self hitTestNodeAtPoint:canvasPt];
    if (hitNode >= 0)
    {
        // Double-click: force-set state.
        if (event.clickCount >= 2)
        {
            if ([(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw respondsToSelector:@selector(graphView:didForceSetState:)])
                [(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw graphView:self didForceSetState:hitNode];
            return;
        }

        // Single click: select + start drag.
        if ([(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw respondsToSelector:@selector(graphView:didSelectState:)])
            [(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw graphView:self didSelectState:hitNode];

        _s.isDraggingNode = YES;
        _s.dragNodeIndex = hitNode;
        _s.dragStartMouse = canvasPt;
        _s.dragStartNodePos = _s.nodePositions[hitNode].pointValue;
        return;
    }

    // Hit test transitions.
    int hitTrans = [self hitTestTransitionAtPoint:canvasPt];
    if (hitTrans >= 0)
    {
        int combined = _s.transStateTransIdx[hitTrans].intValue;
        int sIdx = combined / 1000;
        int tIdx = combined % 1000;
        if ([(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw respondsToSelector:@selector(graphView:didSelectTransition:transitionIndex:)])
            [(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw graphView:self didSelectTransition:sIdx transitionIndex:tIdx];
        return;
    }

    // Clicked empty area: deselect.
    if ([(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw respondsToSelector:@selector(graphView:didSelectState:)])
        [(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw graphView:self didSelectState:-1];
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint viewPt = [self convertPoint:event.locationInWindow fromView:nil];

    if (_s.isPanning)
    {
        _s.panOffset = NSMakePoint(_s.panStartOffset.x + (viewPt.x - _s.panStartMouse.x),
                                 _s.panStartOffset.y + (viewPt.y - _s.panStartMouse.y));
        [self setNeedsDisplay:YES];
        return;
    }

    if (_s.isDraggingNode && _s.dragNodeIndex >= 0 && _s.dragNodeIndex < (int)_s.nodePositions.count)
    {
        NSPoint canvasPt = [self viewToCanvas:viewPt];
        CGFloat dx = canvasPt.x - _s.dragStartMouse.x;
        CGFloat dy = canvasPt.y - _s.dragStartMouse.y;
        NSPoint newPos = NSMakePoint(_s.dragStartNodePos.x + dx, _s.dragStartNodePos.y + dy);
        _s.nodePositions[_s.dragNodeIndex] = [NSValue valueWithPoint:newPos];
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _s.isDraggingNode = NO;
    _s.dragNodeIndex = -1;
    _s.isPanning = NO;
}

- (void)scrollWheel:(NSEvent*)event
{
    CGFloat delta = event.deltaY;
    if (fabs(delta) < 0.01)
        return;

    NSPoint viewPt = [self convertPoint:event.locationInWindow fromView:nil];

    // Zoom toward mouse position.
    CGFloat oldZoom = _s.zoomLevel;
    CGFloat newZoom = oldZoom * (1.0 + delta * 0.05);
    newZoom = MAX(kMinZoom, MIN(kMaxZoom, newZoom));

    // Adjust pan so the point under the cursor stays fixed.
    _s.panOffset = NSMakePoint(viewPt.x - (viewPt.x - _s.panOffset.x) * newZoom / oldZoom,
                             viewPt.y - (viewPt.y - _s.panOffset.y) * newZoom / oldZoom);

    _s.zoomLevel = newZoom;
    [self setNeedsDisplay:YES];
}

- (void)rightMouseDown:(NSEvent*)event
{
    NSPoint viewPt = [self convertPoint:event.locationInWindow fromView:nil];
    NSPoint canvasPt = [self viewToCanvas:viewPt];

    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];

    int hitNode = [self hitTestNodeAtPoint:canvasPt];
    if (hitNode >= 0)
    {
        NSString* title = [NSString
            stringWithFormat:@"Delete \"%@\"",
                             (hitNode < (int)_s.nodeNames.count) ? _s.nodeNames[hitNode] : @"?"];
        NSMenuItem* deleteItem = [[NSMenuItem alloc] initWithTitle:title
                                                            action:@selector(contextDeleteState:)
                                                     keyEquivalent:@""];
        deleteItem.target = self;
        deleteItem.tag = hitNode;
        [menu addItem:deleteItem];
    }
    else
    {
        NSMenuItem* addItem = [[NSMenuItem alloc] initWithTitle:@"Add State"
                                                         action:@selector(contextAddState:)
                                                  keyEquivalent:@""];
        addItem.target = self;
        [menu addItem:addItem];
    }

    [NSMenu popUpContextMenu:menu withEvent:event forView:self];
}

- (void)contextDeleteState:(NSMenuItem*)sender
{
    int idx = (int)sender.tag;
    if ([(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw respondsToSelector:@selector(graphView:didRequestRemoveState:)])
        [(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw graphView:self didRequestRemoveState:idx];
}

- (void)contextAddState:(NSMenuItem*)sender
{
    (void)sender;
    if ([(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw respondsToSelector:@selector(graphViewDidRequestAddState:)])
        [(__bridge id<StateMachineGraphViewDelegate>)_graphDelegateRaw graphViewDidRequestAddState:self];
}

@end

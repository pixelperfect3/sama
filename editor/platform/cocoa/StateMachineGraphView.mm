#import "editor/platform/cocoa/StateMachineGraphView.h"

static const CGFloat kNodeWidth = 120.0;
static const CGFloat kNodeHeight = 50.0;
static const CGFloat kNodeSpacingX = 160.0;
static const CGFloat kNodeSpacingY = 80.0;
static const CGFloat kNodeCornerRadius = 8.0;
static const CGFloat kArrowHeadSize = 8.0;
static const CGFloat kMinZoom = 0.5;
static const CGFloat kMaxZoom = 2.0;

@implementation StateMachineGraphView
{
    __unsafe_unretained id<StateMachineGraphViewDelegate> _graphDelegate;
    NSMutableArray<NSValue*>* _nodePositions;
    NSMutableArray<NSString*>* _nodeNames;
    NSMutableArray<NSString*>* _nodeClipNames;
    NSMutableArray<NSNumber*>* _transFromState;
    NSMutableArray<NSNumber*>* _transToState;
    NSMutableArray<NSString*>* _transCondLabel;
    NSMutableArray<NSNumber*>* _transStateTransIdx;

    int _numStates;
    int _currentStateIndex;
    int _selectedStateIndex;
    int _selectedTransitionIndex;
    CGFloat _zoomLevel;
    NSPoint _panOffset;
    BOOL _isDraggingNode;
    int _dragNodeIndex;
    NSPoint _dragStartMouse;
    NSPoint _dragStartNodePos;
    BOOL _isPanning;
    NSPoint _panStartMouse;
    NSPoint _panStartOffset;
    BOOL _positionsInitialized;
}

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
        _nodePositions = [NSMutableArray array];
        _nodeNames = [NSMutableArray array];
        _nodeClipNames = [NSMutableArray array];
        _transFromState = [NSMutableArray array];
        _transToState = [NSMutableArray array];
        _transCondLabel = [NSMutableArray array];
        _transStateTransIdx = [NSMutableArray array];
        _currentStateIndex = -1;
        _selectedStateIndex = -1;
        _selectedTransitionIndex = -1;
        _numStates = 0;
        _zoomLevel = 1.0;
        _panOffset = NSMakePoint(10.0, 10.0);
        _isDraggingNode = NO;
        _dragNodeIndex = -1;
        _isPanning = NO;
        _positionsInitialized = NO;
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
    _graphDelegate = delegate;
}

- (id<StateMachineGraphViewDelegate>)graphDelegate
{
    return _graphDelegate;
}

- (void)updateWithStateCount:(int)count
                names:(NSArray<NSString*>*)names
            clipNames:(NSArray<NSString*>*)clipNames
         currentState:(int)current
        selectedState:(int)selected
   selectedTransition:(int)selTrans
{
    BOOL countChanged = (count != _numStates);
    _numStates = count;
    _currentStateIndex = current;
    _selectedStateIndex = selected;
    _selectedTransitionIndex = selTrans;

    if (!_nodeNames)
        _nodeNames = [NSMutableArray array];
    if (!_nodeClipNames)
        _nodeClipNames = [NSMutableArray array];
    if (!_nodePositions)
        _nodePositions = [NSMutableArray array];

    [_nodeNames removeAllObjects];
    if (names)
        [_nodeNames addObjectsFromArray:names];
    [_nodeClipNames removeAllObjects];
    if (clipNames)
        [_nodeClipNames addObjectsFromArray:clipNames];

    // Auto-layout: arrange in a grid when positions need initialization.
    if (countChanged || !_positionsInitialized || (int)_nodePositions.count != count)
    {
        [_nodePositions removeAllObjects];
        int cols = MAX(1, (int)ceil(sqrt((double)count)));
        for (int i = 0; i < count; ++i)
        {
            int col = i % cols;
            int row = i / cols;
            CGFloat x = 20.0 + col * kNodeSpacingX;
            CGFloat y = 20.0 + row * kNodeSpacingY;
            [_nodePositions addObject:[NSValue valueWithPoint:NSMakePoint(x, y)]];
        }
        _positionsInitialized = (count > 0);
    }

    [self setNeedsDisplay:YES];
}

- (void)setTransitions:(NSArray<NSNumber*>*)from
                    to:(NSArray<NSNumber*>*)to
                labels:(NSArray<NSString*>*)labels
       stateTransIdxes:(NSArray<NSNumber*>*)stateTransIdxes
{
    if (!_transFromState)
        _transFromState = [NSMutableArray array];
    if (!_transToState)
        _transToState = [NSMutableArray array];
    if (!_transCondLabel)
        _transCondLabel = [NSMutableArray array];
    if (!_transStateTransIdx)
        _transStateTransIdx = [NSMutableArray array];

    [_transFromState removeAllObjects];
    if (from)
        [_transFromState addObjectsFromArray:from];
    [_transToState removeAllObjects];
    if (to)
        [_transToState addObjectsFromArray:to];
    [_transCondLabel removeAllObjects];
    if (labels)
        [_transCondLabel addObjectsFromArray:labels];
    [_transStateTransIdx removeAllObjects];
    if (stateTransIdxes)
        [_transStateTransIdx addObjectsFromArray:stateTransIdxes];
    [self setNeedsDisplay:YES];
}

- (NSPoint)nodeCenterForIndex:(int)idx
{
    if (idx < 0 || idx >= (int)_nodePositions.count)
        return NSZeroPoint;
    NSPoint p = _nodePositions[idx].pointValue;
    return NSMakePoint(p.x + kNodeWidth / 2.0, p.y + kNodeHeight / 2.0);
}

- (NSRect)nodeRectForIndex:(int)idx
{
    if (idx < 0 || idx >= (int)_nodePositions.count)
        return NSZeroRect;
    NSPoint p = _nodePositions[idx].pointValue;
    return NSMakeRect(p.x, p.y, kNodeWidth, kNodeHeight);
}

// Convert a point in view coords to canvas coords (accounting for pan/zoom).
- (NSPoint)viewToCanvas:(NSPoint)viewPt
{
    return NSMakePoint((viewPt.x - _panOffset.x) / _zoomLevel,
                       (viewPt.y - _panOffset.y) / _zoomLevel);
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
    CGContextTranslateCTM(ctx, _panOffset.x, _panOffset.y);
    CGContextScaleCTM(ctx, _zoomLevel, _zoomLevel);

    // Draw transitions (behind nodes).
    for (NSUInteger ti = 0; ti < _transFromState.count; ++ti)
    {
        int fromIdx = _transFromState[ti].intValue;
        int toIdx = _transToState[ti].intValue;
        if (fromIdx < 0 || fromIdx >= _numStates || toIdx < 0 || toIdx >= _numStates)
            continue;

        NSPoint fromCenter = [self nodeCenterForIndex:fromIdx];
        NSPoint toCenter = [self nodeCenterForIndex:toIdx];

        // Determine if this transition is selected.
        int combined = _transStateTransIdx[ti].intValue;
        int tStateIdx = combined / 1000;
        int tTransIdx = combined % 1000;
        BOOL isSelected =
            (tStateIdx == _selectedStateIndex && tTransIdx == _selectedTransitionIndex);

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
            loopPath.lineWidth = 1.5 / _zoomLevel;
            [loopPath stroke];

            // Arrowhead at end.
            [self drawArrowHeadAt:endPt
                        direction:NSMakePoint(endPt.x - cp2.x, endPt.y - cp2.y)
                       isSelected:isSelected];

            // Label at top of loop.
            if (ti < _transCondLabel.count && _transCondLabel[ti].length > 0)
            {
                NSPoint labelPt = NSMakePoint((startPt.x + endPt.x) / 2.0, startPt.y - 45.0);
                [self drawLabel:_transCondLabel[ti] atPoint:labelPt isSelected:isSelected];
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
        arrowPath.lineWidth = 1.5 / _zoomLevel;
        [arrowPath stroke];

        // Arrowhead at end.
        CGFloat adx = endPt.x - cp1.x;
        CGFloat ady = endPt.y - cp1.y;
        [self drawArrowHeadAt:endPt direction:NSMakePoint(adx, ady) isSelected:isSelected];

        // Label at midpoint.
        if (ti < _transCondLabel.count && _transCondLabel[ti].length > 0)
        {
            // Bezier midpoint approximation (t=0.5 with quadratic).
            NSPoint mid = NSMakePoint(0.25 * startPt.x + 0.5 * cp1.x + 0.25 * endPt.x,
                                      0.25 * startPt.y + 0.5 * cp1.y + 0.25 * endPt.y);
            [self drawLabel:_transCondLabel[ti] atPoint:mid isSelected:isSelected];
        }
    }

    // Draw state nodes.
    for (int i = 0; i < _numStates; ++i)
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
        CGFloat borderWidth = 1.5 / _zoomLevel;
        if (i == _currentStateIndex && i == _selectedStateIndex)
        {
            // Both active and selected: use green with blue inner glow.
            borderColor = [NSColor systemGreenColor];
            borderWidth = 2.5 / _zoomLevel;
        }
        else if (i == _currentStateIndex)
        {
            borderColor = [NSColor systemGreenColor];
            borderWidth = 2.0 / _zoomLevel;
        }
        else if (i == _selectedStateIndex)
        {
            borderColor = [NSColor systemBlueColor];
            borderWidth = 2.0 / _zoomLevel;
        }
        else
        {
            borderColor = [NSColor separatorColor];
        }

        [borderColor setStroke];
        nodePath.lineWidth = borderWidth;
        [nodePath stroke];

        // State name (bold, centered).
        NSString* name = (i < (int)_nodeNames.count) ? _nodeNames[i] : @"?";
        NSString* clipName = (i < (int)_nodeClipNames.count) ? _nodeClipNames[i] : @"";

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

    CGFloat sz = kArrowHeadSize / _zoomLevel;
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
    for (int i = _numStates - 1; i >= 0; --i)
    {
        NSRect r = [self nodeRectForIndex:i];
        if (NSPointInRect(canvasPt, r))
            return i;
    }
    return -1;
}

- (int)hitTestTransitionAtPoint:(NSPoint)canvasPt
{
    CGFloat threshold = 8.0 / _zoomLevel;
    for (NSUInteger ti = 0; ti < _transFromState.count; ++ti)
    {
        int fromIdx = _transFromState[ti].intValue;
        int toIdx = _transToState[ti].intValue;
        if (fromIdx < 0 || fromIdx >= _numStates || toIdx < 0 || toIdx >= _numStates)
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
        _isPanning = YES;
        _panStartMouse = viewPt;
        _panStartOffset = _panOffset;
        return;
    }

    // Hit test nodes first.
    int hitNode = [self hitTestNodeAtPoint:canvasPt];
    if (hitNode >= 0)
    {
        // Double-click: force-set state.
        if (event.clickCount >= 2)
        {
            if ([_graphDelegate respondsToSelector:@selector(graphView:didForceSetState:)])
                [_graphDelegate graphView:self didForceSetState:hitNode];
            return;
        }

        // Single click: select + start drag.
        if ([_graphDelegate respondsToSelector:@selector(graphView:didSelectState:)])
            [_graphDelegate graphView:self didSelectState:hitNode];

        _isDraggingNode = YES;
        _dragNodeIndex = hitNode;
        _dragStartMouse = canvasPt;
        _dragStartNodePos = _nodePositions[hitNode].pointValue;
        return;
    }

    // Hit test transitions.
    int hitTrans = [self hitTestTransitionAtPoint:canvasPt];
    if (hitTrans >= 0)
    {
        int combined = _transStateTransIdx[hitTrans].intValue;
        int sIdx = combined / 1000;
        int tIdx = combined % 1000;
        if ([_graphDelegate respondsToSelector:@selector(graphView:didSelectTransition:transitionIndex:)])
            [_graphDelegate graphView:self didSelectTransition:sIdx transitionIndex:tIdx];
        return;
    }

    // Clicked empty area: deselect.
    if ([_graphDelegate respondsToSelector:@selector(graphView:didSelectState:)])
        [_graphDelegate graphView:self didSelectState:-1];
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint viewPt = [self convertPoint:event.locationInWindow fromView:nil];

    if (_isPanning)
    {
        _panOffset = NSMakePoint(_panStartOffset.x + (viewPt.x - _panStartMouse.x),
                                 _panStartOffset.y + (viewPt.y - _panStartMouse.y));
        [self setNeedsDisplay:YES];
        return;
    }

    if (_isDraggingNode && _dragNodeIndex >= 0 && _dragNodeIndex < (int)_nodePositions.count)
    {
        NSPoint canvasPt = [self viewToCanvas:viewPt];
        CGFloat dx = canvasPt.x - _dragStartMouse.x;
        CGFloat dy = canvasPt.y - _dragStartMouse.y;
        NSPoint newPos = NSMakePoint(_dragStartNodePos.x + dx, _dragStartNodePos.y + dy);
        _nodePositions[_dragNodeIndex] = [NSValue valueWithPoint:newPos];
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _isDraggingNode = NO;
    _dragNodeIndex = -1;
    _isPanning = NO;
}

- (void)scrollWheel:(NSEvent*)event
{
    CGFloat delta = event.deltaY;
    if (fabs(delta) < 0.01)
        return;

    NSPoint viewPt = [self convertPoint:event.locationInWindow fromView:nil];

    // Zoom toward mouse position.
    CGFloat oldZoom = _zoomLevel;
    CGFloat newZoom = oldZoom * (1.0 + delta * 0.05);
    newZoom = MAX(kMinZoom, MIN(kMaxZoom, newZoom));

    // Adjust pan so the point under the cursor stays fixed.
    _panOffset = NSMakePoint(viewPt.x - (viewPt.x - _panOffset.x) * newZoom / oldZoom,
                             viewPt.y - (viewPt.y - _panOffset.y) * newZoom / oldZoom);

    _zoomLevel = newZoom;
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
                             (hitNode < (int)_nodeNames.count) ? _nodeNames[hitNode] : @"?"];
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
    if ([_graphDelegate respondsToSelector:@selector(graphView:didRequestRemoveState:)])
        [_graphDelegate graphView:self didRequestRemoveState:idx];
}

- (void)contextAddState:(NSMenuItem*)sender
{
    (void)sender;
    if ([_graphDelegate respondsToSelector:@selector(graphViewDidRequestAddState:)])
        [_graphDelegate graphViewDidRequestAddState:self];
}

@end

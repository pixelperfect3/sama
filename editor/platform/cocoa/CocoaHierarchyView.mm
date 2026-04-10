#include "editor/platform/cocoa/CocoaHierarchyView.h"

#import <Cocoa/Cocoa.h>

// ---------------------------------------------------------------------------
// HierarchyDataSource -- NSTableView data source and delegate.
// ---------------------------------------------------------------------------

static NSPasteboardType const kHierarchyDragType = @"com.nimbus.hierarchy.entity";

@interface HierarchyTableDelegate
    : NSObject <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
@property(nonatomic, strong) NSMutableArray<NSDictionary*>* entities;
@property(nonatomic, assign) uint64_t selectedEntityId;
@property(nonatomic, copy) void (^onSelection)(uint64_t);
@property(nonatomic, copy) void (^onNameChanged)(uint64_t, NSString*);
@property(nonatomic, copy) void (^onReparent)(uint64_t childId, uint64_t parentId);
@property(nonatomic, copy) void (^onCreateChild)(uint64_t parentId);
@property(nonatomic, copy) void (^onDetach)(uint64_t entityId);
@property(nonatomic, copy) void (^onDelete)(uint64_t entityId);
@end

@implementation HierarchyTableDelegate

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        _entities = [NSMutableArray new];
        _selectedEntityId = 0;
    }
    return self;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return (NSInteger)[_entities count];
}

- (NSView*)tableView:(NSTableView*)tableView
    viewForTableColumn:(NSTableColumn*)tableColumn
                   row:(NSInteger)row
{
    NSTableCellView* cell = [tableView makeViewWithIdentifier:tableColumn.identifier owner:self];

    if (!cell)
    {
        cell = [[NSTableCellView alloc] initWithFrame:NSZeroRect];
        cell.identifier = tableColumn.identifier;

        NSTextField* textField;
        bool isNameCol = [tableColumn.identifier isEqualToString:@"name"];

        if (isNameCol)
        {
            // Editable text field for entity names — double-click to edit.
            textField = [[NSTextField alloc] initWithFrame:NSZeroRect];
            textField.bordered = NO;
            textField.drawsBackground = NO;
            textField.editable = YES;
            textField.delegate = self;
            textField.font = [NSFont systemFontOfSize:12.0];
        }
        else
        {
            textField = [NSTextField labelWithString:@""];
            textField.font = [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
            textField.textColor = [NSColor secondaryLabelColor];
        }

        textField.translatesAutoresizingMaskIntoConstraints = NO;
        [cell addSubview:textField];
        cell.textField = textField;

        [NSLayoutConstraint activateConstraints:@[
            [textField.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:4],
            [textField.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-4],
            [textField.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        ]];
    }

    NSDictionary* info = _entities[(NSUInteger)row];

    if ([tableColumn.identifier isEqualToString:@"name"])
    {
        uint32_t depth = [info[@"depth"] unsignedIntValue];
        NSString* name = info[@"name"];
        if (depth > 0)
        {
            NSMutableString* prefix = [NSMutableString string];
            for (uint32_t i = 0; i < depth; ++i)
                [prefix appendString:@"  "];
            name = [NSString stringWithFormat:@"%@%@", prefix, name];
        }
        cell.textField.stringValue = name;
        cell.textField.tag = row;  // store row index for edit callback
    }
    else if ([tableColumn.identifier isEqualToString:@"tags"])
    {
        cell.textField.stringValue = info[@"tags"];
    }

    return cell;
}

// Called when the user finishes editing a name field (press Enter or click away).
- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    NSTextField* textField = notification.object;
    NSInteger row = textField.tag;
    if (row >= 0 && row < (NSInteger)[_entities count])
    {
        NSDictionary* info = _entities[(NSUInteger)row];
        uint64_t eid = [info[@"entityId"] unsignedLongLongValue];
        NSString* newName = textField.stringValue;
        if (_onNameChanged)
        {
            _onNameChanged(eid, newName);
        }
    }
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    NSTableView* tableView = notification.object;
    NSInteger row = [tableView selectedRow];
    if (row >= 0 && row < (NSInteger)[_entities count])
    {
        NSDictionary* info = _entities[(NSUInteger)row];
        uint64_t eid = [info[@"entityId"] unsignedLongLongValue];
        _selectedEntityId = eid;
        if (_onSelection)
        {
            _onSelection(eid);
        }
    }
}

// ---------------------------------------------------------------------------
// Drag-and-drop support
// ---------------------------------------------------------------------------

- (id<NSPasteboardWriting>)tableView:(NSTableView*)tableView pasteboardWriterForRow:(NSInteger)row
{
    if (row < 0 || row >= (NSInteger)[_entities count])
        return nil;

    NSDictionary* info = _entities[(NSUInteger)row];
    uint64_t eid = [info[@"entityId"] unsignedLongLongValue];

    NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
    [item setString:[NSString stringWithFormat:@"%llu", eid] forType:kHierarchyDragType];
    return item;
}

- (NSDragOperation)tableView:(NSTableView*)tableView
                validateDrop:(id<NSDraggingInfo>)info
                 proposedRow:(NSInteger)row
       proposedDropOperation:(NSTableViewDropOperation)dropOperation
{
    // Allow both drop-on (reparent) and drop-between at root (detach).
    return NSDragOperationMove;
}

- (BOOL)tableView:(NSTableView*)tableView
       acceptDrop:(id<NSDraggingInfo>)info
              row:(NSInteger)row
    dropOperation:(NSTableViewDropOperation)dropOperation
{
    NSPasteboard* pb = [info draggingPasteboard];
    NSString* eidStr = [pb stringForType:kHierarchyDragType];
    if (!eidStr)
        return NO;

    uint64_t childId = (uint64_t)[eidStr longLongValue];

    if (dropOperation == NSTableViewDropOn && row >= 0 && row < (NSInteger)[_entities count])
    {
        // Drop on a row -> reparent under that entity.
        NSDictionary* target = _entities[(NSUInteger)row];
        uint64_t parentId = [target[@"entityId"] unsignedLongLongValue];
        if (parentId == childId)
            return NO;  // Can't parent to self.
        if (_onReparent)
            _onReparent(childId, parentId);
        return YES;
    }
    else
    {
        // Drop between rows (or at root level) -> detach.
        if (_onReparent)
            _onReparent(childId, 0);  // 0 == INVALID_ENTITY -> detach
        return YES;
    }
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

- (NSMenu*)menuForRow:(NSInteger)row inTableView:(NSTableView*)tableView
{
    if (row < 0 || row >= (NSInteger)[_entities count])
        return nil;

    NSDictionary* info = _entities[(NSUInteger)row];
    uint64_t eid = [info[@"entityId"] unsignedLongLongValue];
    uint32_t depth = [info[@"depth"] unsignedIntValue];

    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];

    NSMenuItem* createChild = [[NSMenuItem alloc] initWithTitle:@"Create Child Entity"
                                                         action:@selector(contextCreateChild:)
                                                  keyEquivalent:@""];
    createChild.representedObject = @(eid);
    createChild.target = self;
    [menu addItem:createChild];

    if (depth > 0)
    {
        NSMenuItem* detachItem = [[NSMenuItem alloc] initWithTitle:@"Detach from Parent"
                                                            action:@selector(contextDetach:)
                                                     keyEquivalent:@""];
        detachItem.representedObject = @(eid);
        detachItem.target = self;
        [menu addItem:detachItem];
    }

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* deleteItem = [[NSMenuItem alloc] initWithTitle:@"Delete"
                                                        action:@selector(contextDelete:)
                                                 keyEquivalent:@""];
    deleteItem.representedObject = @(eid);
    deleteItem.target = self;
    [menu addItem:deleteItem];

    return menu;
}

- (void)contextCreateChild:(NSMenuItem*)sender
{
    uint64_t eid = [sender.representedObject unsignedLongLongValue];
    if (_onCreateChild)
        _onCreateChild(eid);
}

- (void)contextDetach:(NSMenuItem*)sender
{
    uint64_t eid = [sender.representedObject unsignedLongLongValue];
    if (_onDetach)
        _onDetach(eid);
}

- (void)contextDelete:(NSMenuItem*)sender
{
    uint64_t eid = [sender.representedObject unsignedLongLongValue];
    if (_onDelete)
        _onDelete(eid);
}

@end

// ---------------------------------------------------------------------------
// HierarchyTableView -- custom table view with right-click context menu.
// ---------------------------------------------------------------------------

@interface HierarchyTableView : NSTableView
@end

@implementation HierarchyTableView

- (NSMenu*)menuForEvent:(NSEvent*)event
{
    NSPoint location = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger row = [self rowAtPoint:location];
    if (row >= 0)
    {
        // Select the right-clicked row.
        [self selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
            byExtendingSelection:NO];
    }
    HierarchyTableDelegate* del = (HierarchyTableDelegate*)self.delegate;
    if ([del respondsToSelector:@selector(menuForRow:inTableView:)])
    {
        return [del menuForRow:row inTableView:self];
    }
    return nil;
}

@end

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

namespace engine::editor
{

struct CocoaHierarchyView::Impl
{
    NSScrollView* scrollView = nil;
    NSTableView* tableView = nil;
    HierarchyTableDelegate* delegate = nil;
    SelectionCallback selectionCallback;
    NameChangedCallback nameChangedCallback;
    ReparentCallback reparentCallback;
    CreateChildCallback createChildCallback;
    DetachCallback detachCallback;
    DeleteCallback deleteCallback;
    bool suppressSelectionCallback = false;
};

CocoaHierarchyView::CocoaHierarchyView() : impl_(std::make_unique<Impl>())
{
    @autoreleasepool
    {
        impl_->scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 200, 400)];
        impl_->scrollView.hasVerticalScroller = YES;
        impl_->scrollView.autohidesScrollers = YES;

        impl_->tableView = [[HierarchyTableView alloc] initWithFrame:NSZeroRect];
        impl_->tableView.usesAlternatingRowBackgroundColors = YES;
        impl_->tableView.rowHeight = 20.0;
        impl_->tableView.style = NSTableViewStylePlain;

        // Name column.
        NSTableColumn* nameCol = [[NSTableColumn alloc] initWithIdentifier:@"name"];
        nameCol.title = @"Entity";
        nameCol.minWidth = 80;
        nameCol.width = 120;
        [impl_->tableView addTableColumn:nameCol];

        // Tags column.
        NSTableColumn* tagsCol = [[NSTableColumn alloc] initWithIdentifier:@"tags"];
        tagsCol.title = @"Components";
        tagsCol.minWidth = 50;
        tagsCol.width = 80;
        [impl_->tableView addTableColumn:tagsCol];

        impl_->delegate = [[HierarchyTableDelegate alloc] init];
        impl_->tableView.dataSource = impl_->delegate;
        impl_->tableView.delegate = impl_->delegate;

        // Wire selection callback.
        // No ARC, so capture raw pointer directly (the view outlives the block).
        auto* rawImpl = impl_.get();
        impl_->delegate.onSelection = ^(uint64_t entityId) {
          if (rawImpl && !rawImpl->suppressSelectionCallback && rawImpl->selectionCallback)
          {
              rawImpl->selectionCallback(entityId);
          }
        };

        // Enable drag-and-drop reparenting.
        [impl_->tableView registerForDraggedTypes:@[ kHierarchyDragType ]];
        impl_->tableView.draggingDestinationFeedbackStyle =
            NSTableViewDraggingDestinationFeedbackStyleRegular;

        impl_->scrollView.documentView = impl_->tableView;

        // Header text.
        NSTextField* header = [NSTextField labelWithString:@"Scene Hierarchy"];
        header.font = [NSFont boldSystemFontOfSize:11.0];
        header.translatesAutoresizingMaskIntoConstraints = NO;
        impl_->tableView.headerView.wantsLayer = YES;
    }
}

CocoaHierarchyView::~CocoaHierarchyView()
{
    @autoreleasepool
    {
        if (impl_->tableView)
        {
            impl_->tableView.dataSource = nil;
            impl_->tableView.delegate = nil;
        }
        impl_->scrollView = nil;
        impl_->tableView = nil;
        impl_->delegate = nil;
    }
}

void* CocoaHierarchyView::nativeView() const
{
    return (__bridge void*)impl_->scrollView;
}

void CocoaHierarchyView::setSelectionCallback(SelectionCallback cb)
{
    impl_->selectionCallback = std::move(cb);
}

void CocoaHierarchyView::setNameChangedCallback(NameChangedCallback cb)
{
    impl_->nameChangedCallback = std::move(cb);
    auto* rawImpl = impl_.get();
    impl_->delegate.onNameChanged = ^(uint64_t entityId, NSString* newName) {
      if (rawImpl && rawImpl->nameChangedCallback)
      {
          rawImpl->nameChangedCallback(entityId, [newName UTF8String]);
      }
    };
}

void CocoaHierarchyView::setReparentCallback(ReparentCallback cb)
{
    impl_->reparentCallback = std::move(cb);
    auto* rawImpl = impl_.get();
    impl_->delegate.onReparent = ^(uint64_t childId, uint64_t parentId) {
      if (rawImpl && rawImpl->reparentCallback)
      {
          rawImpl->reparentCallback(childId, parentId);
      }
    };
}

void CocoaHierarchyView::setCreateChildCallback(CreateChildCallback cb)
{
    impl_->createChildCallback = std::move(cb);
    auto* rawImpl = impl_.get();
    impl_->delegate.onCreateChild = ^(uint64_t parentId) {
      if (rawImpl && rawImpl->createChildCallback)
      {
          rawImpl->createChildCallback(parentId);
      }
    };
}

void CocoaHierarchyView::setDetachCallback(DetachCallback cb)
{
    impl_->detachCallback = std::move(cb);
    auto* rawImpl = impl_.get();
    impl_->delegate.onDetach = ^(uint64_t entityId) {
      if (rawImpl && rawImpl->detachCallback)
      {
          rawImpl->detachCallback(entityId);
      }
    };
}

void CocoaHierarchyView::setDeleteCallback(DeleteCallback cb)
{
    impl_->deleteCallback = std::move(cb);
    auto* rawImpl = impl_.get();
    impl_->delegate.onDelete = ^(uint64_t entityId) {
      if (rawImpl && rawImpl->deleteCallback)
      {
          rawImpl->deleteCallback(entityId);
      }
    };
}

void CocoaHierarchyView::setEntities(const std::vector<EntityInfo>& entities)
{
    @autoreleasepool
    {
        [impl_->delegate.entities removeAllObjects];
        for (const auto& info : entities)
        {
            NSDictionary* dict = @{
                @"entityId" : @(info.entityId),
                @"name" : [NSString stringWithUTF8String:info.name.c_str()],
                @"tags" : [NSString stringWithUTF8String:info.tags.c_str()],
                @"depth" : @(info.depth),
            };
            [impl_->delegate.entities addObject:dict];
        }
        [impl_->tableView reloadData];
    }
}

void CocoaHierarchyView::setSelectedEntity(uint64_t entityId)
{
    @autoreleasepool
    {
        impl_->suppressSelectionCallback = true;
        NSInteger idx = -1;
        for (NSUInteger i = 0; i < impl_->delegate.entities.count; ++i)
        {
            uint64_t eid = [impl_->delegate.entities[i][@"entityId"] unsignedLongLongValue];
            if (eid == entityId)
            {
                idx = (NSInteger)i;
                break;
            }
        }

        if (idx >= 0)
        {
            NSIndexSet* indexSet = [NSIndexSet indexSetWithIndex:(NSUInteger)idx];
            [impl_->tableView selectRowIndexes:indexSet byExtendingSelection:NO];
        }
        else
        {
            [impl_->tableView deselectAll:nil];
        }
        impl_->delegate.selectedEntityId = entityId;
        impl_->suppressSelectionCallback = false;
    }
}

}  // namespace engine::editor

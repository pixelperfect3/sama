#include "editor/platform/cocoa/CocoaHierarchyView.h"

#import <Cocoa/Cocoa.h>

// ---------------------------------------------------------------------------
// HierarchyDataSource -- NSTableView data source and delegate.
// ---------------------------------------------------------------------------

@interface HierarchyTableDelegate : NSObject <NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic, strong) NSMutableArray<NSDictionary*>* entities;
@property(nonatomic, assign) uint64_t selectedEntityId;
@property(nonatomic, copy) void (^onSelection)(uint64_t);
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

        NSTextField* textField = [NSTextField labelWithString:@""];
        textField.font = [NSFont systemFontOfSize:12.0];
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
        cell.textField.stringValue = info[@"name"];
    }
    else if ([tableColumn.identifier isEqualToString:@"tags"])
    {
        cell.textField.stringValue = info[@"tags"];
        cell.textField.textColor = [NSColor secondaryLabelColor];
        cell.textField.font = [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    }

    return cell;
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
    bool suppressSelectionCallback = false;
};

CocoaHierarchyView::CocoaHierarchyView() : impl_(std::make_unique<Impl>())
{
    @autoreleasepool
    {
        impl_->scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 200, 400)];
        impl_->scrollView.hasVerticalScroller = YES;
        impl_->scrollView.autohidesScrollers = YES;

        impl_->tableView = [[NSTableView alloc] initWithFrame:NSZeroRect];
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

/* -*- Mode: javascript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const Ci = Components.interfaces;
const Cu = Components.utils;

const ENSURE_SELECTION_VISIBLE_DELAY = 50; // ms

Cu.import("resource:///modules/devtools/ViewHelpers.jsm");

this.EXPORTED_SYMBOLS = ["SideMenuWidget"];

/**
 * A simple side menu, with the ability of grouping menu items.
 *
 * You can use this widget alone, but it works great with a MenuContainer!
 * In that case, you should never need to access the methods in the
 * SideMenuWidget directly, use the wrapper MenuContainer instance instead.
 *
 * @see ViewHelpers.jsm
 *
 * function MyView() {
 *   this.node = new SideMenuWidget(document.querySelector(".my-node"));
 * }
 * ViewHelpers.create({ constructor: MyView, proto: MenuContainer.prototype }, {
 *   myMethod: function() {},
 *   ...
 * });
 *
 * @param nsIDOMNode aNode
 *        The element associated with the widget.
 * @param boolean aShowArrows
 *        Specifies if items in this container should display horizontal arrows.
 */
this.SideMenuWidget = function SideMenuWidget(aNode, aShowArrows = true) {
  this.document = aNode.ownerDocument;
  this.window = this.document.defaultView;
  this._parent = aNode;
  this._showArrows = aShowArrows;

  // Create an internal scrollbox container.
  this._list = this.document.createElement("scrollbox");
  this._list.className = "side-menu-widget-container";
  this._list.setAttribute("flex", "1");
  this._list.setAttribute("orient", "vertical");
  this._list.setAttribute("with-arrow", aShowArrows);
  this._parent.appendChild(this._list);
  this._boxObject = this._list.boxObject.QueryInterface(Ci.nsIScrollBoxObject);

  // Menu items can optionally be grouped.
  this._groupsByName = new Map(); // Can't use a WeakMap because keys are strings.
  this._orderedGroupElementsArray = [];
  this._orderedMenuElementsArray = [];

  // Delegate some of the associated node's methods to satisfy the interface
  // required by MenuContainer instances.
  ViewHelpers.delegateWidgetEventMethods(this, aNode);
};

SideMenuWidget.prototype = {
  /**
   * Specifies if groups in this container should be sorted alphabetically.
   */
  sortedGroups: true,

  /**
   * Specifies if this container should try to keep the selected item visible.
   * (For example, when new items are added the selection is brought into view).
   */
  maintainSelectionVisible: true,

  /**
   * Specifies that the container viewport should be "stuck" to the
   * bottom. That is, the container is automatically scrolled down to
   * keep appended items visible, but only when the scroll position is
   * already at the bottom.
   */
  autoscrollWithAppendedItems: false,

  /**
   * Inserts an item in this container at the specified index, optionally
   * grouping by name.
   *
   * @param number aIndex
   *        The position in the container intended for this item.
   * @param string | nsIDOMNode aContents
   *        The string or node displayed in the container.
   * @param string aTooltip [optional]
   *        A tooltip attribute for the displayed item.
   * @param string aGroup [optional]
   *        The group to place the displayed item into.
   * @return nsIDOMNode
   *         The element associated with the displayed item.
   */
  insertItemAt: function SMW_insertItemAt(aIndex, aContents, aTooltip = "", aGroup = "") {
    // Invalidate any notices set on this widget.
    this.removeAttribute("notice");

    let maintainScrollAtBottom =
      this.autoscrollWithAppendedItems &&
      (aIndex < 0 || aIndex >= this._orderedMenuElementsArray.length) &&
      (this._list.scrollTop + this._list.clientHeight >= this._list.scrollHeight);

    let group = this._getGroupForName(aGroup);
    let item = this._getItemForGroup(group, aContents, aTooltip);
    let element = item.insertSelfAt(aIndex);

    if (this.maintainSelectionVisible) {
      this.ensureSelectionIsVisible({ withGroup: true, delayed: true });
    }
    if (maintainScrollAtBottom) {
      this._list.scrollTop = this._list.scrollHeight;
    }

    return element;
  },

  /**
   * Returns the child node in this container situated at the specified index.
   *
   * @param number aIndex
   *        The position in the container intended for this item.
   * @return nsIDOMNode
   *         The element associated with the displayed item.
   */
  getItemAtIndex: function SMW_getItemAtIndex(aIndex) {
    return this._orderedMenuElementsArray[aIndex];
  },

  /**
   * Removes the specified child node from this container.
   *
   * @param nsIDOMNode aChild
   *        The element associated with the displayed item.
   */
  removeChild: function SMW_removeChild(aChild) {
    if (aChild.className == "side-menu-widget-item-contents") {
      // Remove the item itself, not the contents.
      aChild.parentNode.remove();
    } else {
      // Groups with no title don't have any special internal structure.
      aChild.remove();
    }

    this._orderedMenuElementsArray.splice(
      this._orderedMenuElementsArray.indexOf(aChild), 1);

    if (this._selectedItem == aChild) {
      this._selectedItem = null;
    }
  },

  /**
   * Removes all of the child nodes from this container.
   */
  removeAllItems: function SMW_removeAllItems() {
    let parent = this._parent;
    let list = this._list;

    while (list.hasChildNodes()) {
      list.firstChild.remove();
    }

    this._selectedItem = null;

    this._groupsByName.clear();
    this._orderedGroupElementsArray.length = 0;
    this._orderedMenuElementsArray.length = 0;
  },

  /**
   * Gets the currently selected child node in this container.
   * @return nsIDOMNode
   */
  get selectedItem() this._selectedItem,

  /**
   * Sets the currently selected child node in this container.
   * @param nsIDOMNode aChild
   */
  set selectedItem(aChild) {
    let menuArray = this._orderedMenuElementsArray;

    if (!aChild) {
      this._selectedItem = null;
    }
    for (let node of menuArray) {
      if (node == aChild) {
        node.classList.add("selected");
        node.parentNode.classList.add("selected");
        this._selectedItem = node;
      } else {
        node.classList.remove("selected");
        node.parentNode.classList.remove("selected");
      }
    }

    // Repeated calls to ensureElementIsVisible would interfere with each other
    // and may sometimes result in incorrect scroll positions.
    this.ensureSelectionIsVisible({ delayed: true });
  },

  /**
   * Ensures the selected element is visible.
   * @see SideMenuWidget.prototype.ensureElementIsVisible.
   */
  ensureSelectionIsVisible: function SMW_ensureSelectionIsVisible(aFlags) {
    this.ensureElementIsVisible(this.selectedItem, aFlags);
  },

  /**
   * Ensures the specified element is visible.
   *
   * @param nsIDOMNode aElement
   *        The element to make visible.
   * @param object aFlags [optional]
   *        An object containing some of the following flags:
   *        - withGroup: true if the group header should also be made visible, if possible
   *        - delayed: wait a few cycles before ensuring the selection is visible
   */
  ensureElementIsVisible: function SMW_ensureElementIsVisible(aElement, aFlags = {}) {
    if (!aElement) {
      return;
    }

    if (aFlags.delayed) {
      delete aFlags.delayed;
      this.window.clearTimeout(this._ensureVisibleTimeout);
      this._ensureVisibleTimeout = this.window.setTimeout(() => {
        this.ensureElementIsVisible(aElement, aFlags);
      }, ENSURE_SELECTION_VISIBLE_DELAY);
      return;
    }

    if (aFlags.withGroup) {
      let groupList = aElement.parentNode;
      let groupContainer = groupList.parentNode;
      groupContainer.scrollIntoView(true); // Align with the top.
    }

    this._boxObject.ensureElementIsVisible(aElement);
  },

  /**
   * Shows all the groups, even the ones with no visible children.
   */
  showEmptyGroups: function SMW_showEmptyGroups() {
    for (let group of this._orderedGroupElementsArray) {
      group.hidden = false;
    }
  },

  /**
   * Hides all the groups which have no visible children.
   */
  hideEmptyGroups: function SMW_hideEmptyGroups() {
    let visibleChildNodes = ".side-menu-widget-item-contents:not([hidden=true])";

    for (let group of this._orderedGroupElementsArray) {
      group.hidden = group.querySelectorAll(visibleChildNodes).length == 0;
    }
    for (let menuItem of this._orderedMenuElementsArray) {
      menuItem.parentNode.hidden = menuItem.hidden;
    }
  },

  /**
   * Returns the value of the named attribute on this container.
   *
   * @param string aName
   *        The name of the attribute.
   * @return string
   *         The current attribute value.
   */
  getAttribute: function SMW_getAttribute(aName) {
    return this._parent.getAttribute(aName);
  },

  /**
   * Adds a new attribute or changes an existing attribute on this container.
   *
   * @param string aName
   *        The name of the attribute.
   * @param string aValue
   *        The desired attribute value.
   */
  setAttribute: function SMW_setAttribute(aName, aValue) {
    this._parent.setAttribute(aName, aValue);

    if (aName == "notice") {
      this.notice = aValue;
    }
  },

  /**
   * Removes an attribute on this container.
   *
   * @param string aName
   *        The name of the attribute.
   */
  removeAttribute: function SMW_removeAttribute(aName) {
    this._parent.removeAttribute(aName);

    if (aName == "notice") {
      this._removeNotice();
    }
  },

  /**
   * Sets the text displayed in this container as a notice.
   * @param string aValue
   */
  set notice(aValue) {
    if (this._noticeTextNode) {
      this._noticeTextNode.setAttribute("value", aValue);
    }
    this._noticeTextValue = aValue;
    this._appendNotice();
  },

  /**
   * Creates and appends a label representing a notice in this container.
   */
  _appendNotice: function DVSL__appendNotice() {
    if (this._noticeTextNode || !this._noticeTextValue) {
      return;
    }

    let container = this.document.createElement("vbox");
    container.className = "side-menu-widget-empty-notice-container";
    container.setAttribute("align", "center");

    let label = this.document.createElement("label");
    label.className = "plain side-menu-widget-empty-notice";
    label.setAttribute("value", this._noticeTextValue);
    container.appendChild(label);

    this._parent.insertBefore(container, this._list);
    this._noticeTextContainer = container;
    this._noticeTextNode = label;
  },

  /**
   * Removes the label representing a notice in this container.
   */
  _removeNotice: function DVSL__removeNotice() {
    if (!this._noticeTextNode) {
      return;
    }

    this._parent.removeChild(this._noticeTextContainer);
    this._noticeTextContainer = null;
    this._noticeTextNode = null;
  },

  /**
   * Gets a container representing a group for menu items. If the container
   * is not available yet, it is immediately created.
   *
   * @param string aName
   *        The required group name.
   * @return SideMenuGroup
   *         The newly created group.
   */
  _getGroupForName: function SMW__getGroupForName(aName) {
    let cachedGroup = this._groupsByName.get(aName);
    if (cachedGroup) {
      return cachedGroup;
    }

    let group = new SideMenuGroup(this, aName);
    this._groupsByName.set(aName, group);
    group.insertSelfAt(this.sortedGroups ? group.findExpectedIndexForSelf() : -1);
    return group;
  },

  /**
   * Gets a menu item to be displayed inside a group.
   * @see SideMenuWidget.prototype._getGroupForName
   *
   * @param SideMenuGroup aGroup
   *        The group to contain the menu item.
   * @param string | nsIDOMNode aContents
   *        The string or node displayed in the container.
   * @param string aTooltip [optional]
   *        A tooltip attribute for the displayed item.
   */
  _getItemForGroup: function SMW__getItemForGroup(aGroup, aContents, aTooltip) {
    return new SideMenuItem(aGroup, aContents, aTooltip, this._showArrows);
  },

  window: null,
  document: null,
  _showArrows: false,
  _parent: null,
  _list: null,
  _boxObject: null,
  _selectedItem: null,
  _groupsByName: null,
  _orderedGroupElementsArray: null,
  _orderedMenuElementsArray: null,
  _ensureVisibleTimeout: null,
  _noticeTextContainer: null,
  _noticeTextNode: null,
  _noticeTextValue: ""
};

/**
 * A SideMenuGroup constructor for the BreadcrumbsWidget.
 * Represents a group which should contain SideMenuItems.
 *
 * @param SideMenuWidget aWidget
 *        The widget to contain this menu item.
 * @param string aName
 *        The string displayed in the container.
 */
function SideMenuGroup(aWidget, aName) {
  this.document = aWidget.document;
  this.window = aWidget.window;
  this.ownerView = aWidget;
  this.identifier = aName;

  // Create an internal title and list container.
  if (aName) {
    let target = this._target = this.document.createElement("vbox");
    target.className = "side-menu-widget-group";
    target.setAttribute("name", aName);
    target.setAttribute("tooltiptext", aName);

    let list = this._list = this.document.createElement("vbox");
    list.className = "side-menu-widget-group-list";

    let title = this._title = this.document.createElement("hbox");
    title.className = "side-menu-widget-group-title";

    let name = this._name = this.document.createElement("label");
    name.className = "plain name";
    name.setAttribute("value", aName);
    name.setAttribute("crop", "end");
    name.setAttribute("flex", "1");

    title.appendChild(name);
    target.appendChild(title);
    target.appendChild(list);
  }
  // Skip a few redundant nodes when no title is shown.
  else {
    let target = this._target = this._list = this.document.createElement("vbox");
    target.className = "side-menu-widget-group side-menu-widget-group-list";
  }
}

SideMenuGroup.prototype = {
  get _orderedGroupElementsArray() this.ownerView._orderedGroupElementsArray,
  get _orderedMenuElementsArray() this.ownerView._orderedMenuElementsArray,

  /**
   * Inserts this group in the parent container at the specified index.
   *
   * @param number aIndex
   *        The position in the container intended for this group.
   */
  insertSelfAt: function SMG_insertSelfAt(aIndex) {
    let ownerList = this.ownerView._list;
    let groupsArray = this._orderedGroupElementsArray;

    if (aIndex >= 0) {
      ownerList.insertBefore(this._target, groupsArray[aIndex]);
      groupsArray.splice(aIndex, 0, this._target);
    } else {
      ownerList.appendChild(this._target);
      groupsArray.push(this._target);
    }
  },

  /**
   * Finds the expected index of this group based on its name.
   *
   * @return number
   *         The expected index.
   */
  findExpectedIndexForSelf: function SMG_findExpectedIndexForSelf() {
    let identifier = this.identifier;
    let groupsArray = this._orderedGroupElementsArray;

    for (let group of groupsArray) {
      let name = group.getAttribute("name");
      if (name > identifier && // Insertion sort at its best :)
         !name.contains(identifier)) { // Least significat group should be last.
        return groupsArray.indexOf(group);
      }
    }
    return -1;
  },

  window: null,
  document: null,
  ownerView: null,
  identifier: "",
  _target: null,
  _title: null,
  _name: null,
  _list: null
};

/**
 * A SideMenuItem constructor for the BreadcrumbsWidget.
 *
 * @param SideMenuGroup aGroup
 *        The group to contain this menu item.
 * @param string aTooltip [optional]
 *        A tooltip attribute for the displayed item.
 * @param string | nsIDOMNode aContents
 *        The string or node displayed in the container.
 * @param boolean aArrowFlag
 *        True if a horizontal arrow should be shown.
 */
function SideMenuItem(aGroup, aContents, aTooltip, aArrowFlag) {
  this.document = aGroup.document;
  this.window = aGroup.window;
  this.ownerView = aGroup;

  // Show a horizontal arrow towards the content.
  if (aArrowFlag) {
    let container = this._container = this.document.createElement("hbox");
    container.className = "side-menu-widget-item";
    container.setAttribute("tooltiptext", aTooltip);

    let target = this._target = this.document.createElement("vbox");
    target.className = "side-menu-widget-item-contents";

    let arrow = this._arrow = this.document.createElement("hbox");
    arrow.className = "side-menu-widget-item-arrow";

    container.appendChild(target);
    container.appendChild(arrow);
  }
  // Skip a few redundant nodes when no horizontal arrow is shown.
  else {
    let target = this._target = this._container = this.document.createElement("hbox");
    target.className = "side-menu-widget-item side-menu-widget-item-contents";
  }

  this._target.setAttribute("flex", "1");
  this.contents = aContents;
}

SideMenuItem.prototype = {
  get _orderedGroupElementsArray() this.ownerView._orderedGroupElementsArray,
  get _orderedMenuElementsArray() this.ownerView._orderedMenuElementsArray,

  /**
   * Inserts this item in the parent group at the specified index.
   *
   * @param number aIndex
   *        The position in the container intended for this item.
   * @return nsIDOMNode
   *         The element associated with the displayed item.
   */
  insertSelfAt: function SMI_insertSelfAt(aIndex) {
    let ownerList = this.ownerView._list;
    let menuArray = this._orderedMenuElementsArray;

    if (aIndex >= 0) {
      ownerList.insertBefore(this._container, ownerList.childNodes[aIndex]);
      menuArray.splice(aIndex, 0, this._target);
    } else {
      ownerList.appendChild(this._container);
      menuArray.push(this._target);
    }

    return this._target;
  },

  /**
   * Sets the contents displayed in this item's view.
   *
   * @param string | nsIDOMNode aContents
   *        The string or node displayed in the container.
   */
  set contents(aContents) {
    // If this item's view contents are a string, then create a label to hold
    // the text displayed in this breadcrumb.
    if (typeof aContents == "string") {
      let label = this.document.createElement("label");
      label.className = "side-menu-widget-item-label";
      label.setAttribute("value", aContents);
      label.setAttribute("crop", "start");
      label.setAttribute("flex", "1");
      this.contents = label;
      return;
    }
    // If there are already some contents displayed, replace them.
    if (this._target.hasChildNodes()) {
      this._target.replaceChild(aContents, this._target.firstChild);
      return;
    }
    // These are the first contents ever displayed.
    this._target.appendChild(aContents);
  },

  window: null,
  document: null,
  ownerView: null,
  _target: null,
  _container: null,
  _arrow: null
};

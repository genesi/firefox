/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsMenuObject_h__
#define __nsMenuObject_h__

#include "nsIContent.h"
#include "imgINotificationObserver.h"
#include "nsAutoPtr.h"
#include "imgRequestProxy.h"
#include "nsRect.h"
#include "nsCOMPtr.h"
#include "nsTArray.h"
#include "nsIURI.h"
#include "mozilla/Attributes.h"

#include "nsNativeMenuDocListener.h"
#include "nsDbusmenu.h"

class nsIAtom;
class nsMenuBar;
class nsMenuObjectContainer;

/*
 * This is the base class for all menu nodes. Each instance represents
 * a single node in the menu hierarchy. It wraps the corresponding DOM node and
 * native menu node, keeps them in sync and transfers events between the two.
 * It is reference counted only for the purpose of guaranteeing it stays alive
 * during asynchronous operations. Each node is owned by its parent (the top
 * level menubar is owned by the window) and keeps a weak reference to its
 * parent. Nodes must not outlive their parents, so it is not safe to hold a
 * strong reference to this externally. If you hold a strong reference internally
 * during an asynchronous operation, consider that the parent might disappear
 * before this.
 */
class nsMenuObject : public nsNativeMenuChangeObserver
{
public:
    NS_INLINE_DECL_REFCOUNTING(nsMenuObject)

    enum EType {
        eType_MenuBar,
        eType_Menu,
        eType_MenuItem,
        eType_MenuSeparator
    };

    enum PropertyFlags {
        ePropLabel = (1 << 0),
        ePropEnabled = (1 << 1),
        ePropVisible = (1 << 2),
        ePropIconData = (1 << 3),
        ePropType = (1 << 4),
        ePropShortcut = (1 << 5),
        ePropToggleType = (1 << 6),
        ePropToggleState = (1 << 7),
        ePropChildDisplay = (1 << 8)
    };

    virtual ~nsMenuObject();

    // Get the native menu item node. Will return null if not created yet
    DbusmenuMenuitem* GetNativeData() { return mNativeData; }

    // Get the parent menu object
    nsMenuObjectContainer* Parent() const { return mParent; }

    // Get the content node
    nsIContent* ContentNode() const { return mContent; }

    // Get the type of this node. Must be provided by subclasses
    virtual EType Type() const = 0;

    // Get the top-level menubar that this object belongs to
    nsMenuBar* MenuBar();

    // Get the document listener
    nsNativeMenuDocListener* DocListener() { return mListener; }

    // Create the native menu item node (called by containers)
    void CreateNativeData();

    // Adopt the specified native menu item node (called by containers)
    nsresult AdoptNativeData(DbusmenuMenuitem *aNativeData);

    // Update properties that should be refreshed when the container opens
    virtual void Update() { };

protected:
    nsMenuObject() :
        mNativeData(nullptr),
        mParent(nullptr) { };
    nsresult Init(nsMenuObjectContainer *aParent, nsIContent *aContent);

    void SyncLabelFromContent();
    void SyncVisibilityFromContent();
    void SyncSensitivityFromContent();
    void SyncIconFromContent();
   
    DbusmenuMenuitem *mNativeData;
    nsCOMPtr<nsIContent> mContent;
    nsRefPtr<nsNativeMenuDocListener> mListener;
    nsMenuObjectContainer *mParent;

private:
    friend class IconLoader;

    class IconLoader MOZ_FINAL : public imgINotificationObserver {
    public:
        NS_DECL_ISUPPORTS
        NS_DECL_IMGINOTIFICATIONOBSERVER

        IconLoader(nsMenuObject *aOwner) : mOwner(aOwner),
                                           mIconLoaded(false) { };

        void ScheduleIconLoad();
        void LoadIcon();
        void Destroy();

    private:
        ~IconLoader() { };

        nsMenuObject *mOwner;
        nsRefPtr<imgRequestProxy> mImageRequest;
        nsCOMPtr<nsIURI> mURI;
        nsIntRect mImageRect;
        uint8_t mIconLoaded;
    };

    // Set up initial properties on the native data.
    // This should be implemented by subclasses
    virtual void InitializeNativeData() { };

    // Return the properties that this menu object type supports
    // This should be implemented by subclasses
    virtual PropertyFlags SupportedProperties() const {
        return PropertyFlags(0);
    }

    // Determine whether this menu object could use the specified
    // native item. Returns true by default but can be overridden by subclasses
    virtual bool IsCompatibleWithNativeData(DbusmenuMenuitem *aNativeData) { return true; }

    void RemoveUnsupportedProperties();
    bool ShouldShowIcon() const;
    void ClearIcon();

    nsRefPtr<IconLoader> mIconLoader;
};

// Base class for containers (menus and menubars)
class nsMenuObjectContainer : public nsMenuObject
{
public:
    typedef nsTArray<nsRefPtr<nsMenuObject> > storage_type;

    // Determine if this container is being displayed on screen. Must be
    // implemented by subclasses
    virtual bool IsBeingDisplayed() const = 0;

    // Return the first previous sibling that is of a type supported by the
    // menu system
    static nsIContent* GetPreviousSupportedSibling(nsIContent *aContent);

    static const storage_type::index_type NoIndex;

protected:
    nsMenuObjectContainer() : nsMenuObject() { };

    already_AddRefed<nsMenuObject> CreateChild(nsIContent *aContent, nsresult *aRv);
    uint32_t IndexOf(nsIContent *aChild);

    storage_type mMenuObjects;
};

#endif /* __nsMenuObject_h__ */

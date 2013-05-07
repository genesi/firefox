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
class nsMenuObjectIconLoader;

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
class nsMenuObject
{
public:
    NS_INLINE_DECL_REFCOUNTING(nsMenuObject)

    enum EType {
        eType_MenuBar,
        eType_Menu,
        eType_MenuItem,
        eType_MenuSeparator
    };

    enum ERefreshType {
        eRefreshType_Full,
        eRefreshType_ContainerOpening
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
    nsMenuObject* Parent() const { return mParent; }

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

    // Signal that the containing menu is going to be displayed on screen
    void ContainerIsOpening();

    virtual void OnAttributeChanged(nsIContent *aContent, nsIAtom *aAttribute) {
        NS_ERROR("Unhandled AttributeChanged() notification");
    }
    virtual void OnContentInserted(nsIContent *aContainer,
                                   nsIContent *aChild,
                                   nsIContent *aPrevSibling) {
        NS_ERROR("Unhandled ContentInserted() notification");
    }
    virtual void OnContentRemoved(nsIContent *aContainer, nsIContent *aChild) {
        NS_ERROR("Unhandled ContentRemoved() notification");
    }
    virtual void BeginUpdateBatch(nsIContent *aContent) { };
    virtual void EndUpdateBatch() { };

protected:
    nsMenuObject() :
        mNativeData(nullptr),
        mParent(nullptr) { };
    nsresult Init(nsMenuObject *aParent, nsIContent *aContent);

    void SyncLabelFromContent();
    void SyncVisibilityFromContent();
    void SyncSensitivityFromContent();
    void SyncIconFromContent();
   
    DbusmenuMenuitem *mNativeData;
    nsCOMPtr<nsIContent> mContent;
    nsRefPtr<nsNativeMenuDocListener> mListener;
    nsMenuObject *mParent;

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

    virtual void InitializeNativeData() { };
    virtual void Refresh(ERefreshType aType) { };
    virtual PropertyFlags SupportedProperties() const {
        return PropertyFlags(0);
    }
    virtual nsresult ImplInit() { return NS_OK; }
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

    already_AddRefed<nsMenuObject> CreateChild(nsIContent *aContent, nsresult *aRv);

    static nsIContent* GetPreviousSupportedSibling(nsIContent *aContent);

    static const storage_type::index_type NoIndex;

protected:
    nsMenuObjectContainer() : nsMenuObject() { };
    uint32_t IndexOf(nsIContent *aChild);

    storage_type mMenuObjects;
};

#endif /* __nsMenuObject_h__ */

/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsMenuBar_h__
#define __nsMenuBar_h__

#include "nsCOMPtr.h"
#include "nsIDOMEventTarget.h"
#include "nsString.h"
#include "nsIDOMEventListener.h"
#include "mozilla/Attributes.h"

#include "nsNativeMenuUtils.h"
#include "nsMenuObject.h"
#include "nsDbusmenu.h"

#include <gtk/gtk.h>

class nsIWidget;
class nsIContent;
class nsIDOMEvent;
class nsIDOMKeyEvent;

/*
 * The menubar class. There is one of these per window (and the window
 * owns its menubar). Each menubar has an object path, and the service is
 * responsible for telling the desktop shell which object path corresponds
 * to a particular window. A menubar and its hierarchy also own a
 * nsNativeMenuDocListener.
 */
class nsMenuBar MOZ_FINAL : public nsMenuObjectContainer
{
public:
    ~nsMenuBar();

    static already_AddRefed<nsMenuBar> Create(nsIWidget *aParent,
                                              nsIContent *aMenuBarNode);

    nsMenuObject::EType Type() const { return nsMenuObject::eType_MenuBar; }

    bool IsBeingDisplayed() const { return true; }

    // Get the native window ID for this menubar
    uint32_t WindowId() const;

    // Get the object path for this menubar
    nsAdoptingCString ObjectPath() const;

    // Initializes and returns a cancellable request object, used
    // by the menuservice when registering this menubar
    nsNativeMenuGIORequest& BeginRegisterRequest() {
        mRegisterRequestCanceller.Start();
        return mRegisterRequestCanceller;
    }

    // Finishes the current request to register the menubar
    void EndRegisterRequest() {
        NS_ASSERTION(RegisterRequestInProgress(), "No request in progress");
        mRegisterRequestCanceller.Finish();
    }

    bool RegisterRequestInProgress() const {
        return mRegisterRequestCanceller.InProgress();
    }

    // Get the top-level GtkWindow handle
    GtkWidget* TopLevelWindow() { return mTopLevel; }

    // Called from the menuservice when the menubar is about to be registered.
    // Causes the native menubar to be created, and the XUL menubar to be hidden
    nsresult Activate();

    // Called from the menuservice when the menubar is no longer registered
    // with the desktop shell. Will cause the XUL menubar to be shown again
    void Deactivate();

    void OnAttributeChanged(nsIContent *aContent, nsIAtom *aAttribute);
    void OnContentInserted(nsIContent *aContainer, nsIContent *aChild,
                           nsIContent *aPrevSibling);
    void OnContentRemoved(nsIContent *aContainer, nsIContent *aChild);

private:
    enum {
        eFlag_Active = 1 << 0
    };

    enum ModifierFlags {
        eModifierShift = (1 << 0),
        eModifierCtrl = (1 << 1),
        eModifierAlt = (1 << 2),
        eModifierMeta = (1 << 3)
    };

    class DocumentEventListener MOZ_FINAL : public nsIDOMEventListener {
    public:
        NS_DECL_ISUPPORTS
        NS_DECL_NSIDOMEVENTLISTENER

        DocumentEventListener(nsMenuBar *aOwner) : mOwner(aOwner) { };
        ~DocumentEventListener() { };

    private:
        nsMenuBar *mOwner;
    };

    nsMenuBar();
    nsresult Init(nsIWidget *aParent, nsIContent *aMenuBarNode);
    nsresult RemoveMenuObjectAt(uint32_t aIndex);
    nsresult RemoveMenuObject(nsIContent *aChild);
    nsresult InsertMenuObjectAfter(nsMenuObject *aChild, nsIContent *aPrevSibling);
    nsresult AppendMenuObject(nsMenuObject *aChild);
    nsresult Build();
    void DisconnectDocumentEventListeners();
    void SetShellShowingMenuBar(bool aShowing);
    void Focus();
    void Blur();
    ModifierFlags GetModifiersFromEvent(nsIDOMKeyEvent *aEvent);
    nsresult Keypress(nsIDOMEvent *aEvent);
    nsresult KeyDown(nsIDOMEvent *aEvent);
    nsresult KeyUp(nsIDOMEvent *aEvent);

    bool IsActive() const {
        return mFlags & eFlag_Active;
    }
    void SetIsActiveFlag() {
        mFlags |= eFlag_Active;
    }
    void ClearIsActiveFlag() {
        mFlags &= ~eFlag_Active;
    }

    GtkWidget *mTopLevel;
    DbusmenuServer *mServer;
    nsCOMPtr<nsIDOMEventTarget> mDocument;
    nsNativeMenuGIORequest mRegisterRequestCanceller;
    nsRefPtr<DocumentEventListener> mEventListener;

    uint32_t mAccessKey;
    ModifierFlags mAccessKeyMask;
    uint8_t mFlags;
};

#endif /* __nsMenuBar_h__ */

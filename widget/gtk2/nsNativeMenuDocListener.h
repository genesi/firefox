/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsNativeMenuDocListener_h__
#define __nsNativeMenuDocListener_h__

#include "nsStubMutationObserver.h"
#include "nsTObserverArray.h"
#include "nsClassHashtable.h"
#include "nsTArray.h"
#include "nsAutoPtr.h"
#include "nsIContent.h"
#include "nsIAtom.h"
#include "mozilla/Attributes.h"

class nsIDocument;
class nsMenuObject;

class nsNativeMenuAutoSuspendMutations;
class nsNativeMenuDocListenerFlushRunnable;
class DispatchContext;

/*
 * This class keeps a mapping of content nodes to native menu objects,
 * and forwards DOM mutations to registered menu objects. There is exactly
 * one of these for every menubar.
 */
class nsNativeMenuDocListener MOZ_FINAL : nsStubMutationObserver
{
public:
    typedef nsTArray<nsNativeMenuDocListener *> listener_array_type;
    typedef nsTObserverArray<nsMenuObject *> observer_array_type;

    NS_DECL_ISUPPORTS
    NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED
    NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
    NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
    NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED
    NS_DECL_NSIMUTATIONOBSERVER_NODEWILLBEDESTROYED

    static already_AddRefed<nsNativeMenuDocListener> Create(nsIContent *aRootNode);

    // Register a menu object to receive mutation events for the specified
    // content node. The caller must keep the menu object alive until
    // UnregisterForContentChanges is called.
    void RegisterForContentChanges(nsIContent *aContent,
                                   nsMenuObject *aMenuObject);

    // Unregister the menu object from receiving mutation events for the
    // specified content node.
    void UnregisterForContentChanges(nsIContent *aContent,
                                     nsMenuObject *aMenuObject);

    // Start listening to the document and forwarding DOM mutations to
    // registered menu objects.
    void Start();

    // Stop listening to the document. No DOM mutations will be forwarded
    // to registered menu objects.
    void Stop();

private:
    friend class nsNativeMenuAutoSuspendMutations;
    friend class DispatchContext;

    struct MutationRecord {
        enum RecordType {
            eAttributeChanged,
            eContentInserted,
            eContentRemoved
        } mType;

        nsCOMPtr<nsIContent> mTarget;
        nsCOMPtr<nsIContent> mChild;
        nsCOMPtr<nsIContent> mPrevSibling;
        nsCOMPtr<nsIAtom> mAttribute;
    };

    nsNativeMenuDocListener();
    ~nsNativeMenuDocListener();
    nsresult Init(nsIContent *aRootNode);

    void DoAttributeChanged(nsIContent *aContent, nsIAtom *aAttribute);
    void DoContentInserted(nsIContent *aContainer,
                           nsIContent *aChild,
                           nsIContent *aPrevSibling);
    void DoContentRemoved(nsIContent *aContainer, nsIContent *aChild);
    void DoBeginUpdateBatch(nsIContent *aTarget);
    void DoEndUpdateBatch(nsIContent *aTarget);
    void FlushPendingMutations();
    static void ScheduleListener(nsNativeMenuDocListener *aListener);
    static void RemoveListener(nsNativeMenuDocListener *aListener);
    static void AddMutationBlocker() { ++sInhibitDepth; }
    static void RemoveMutationBlocker();

    nsCOMPtr<nsIContent> mRootNode;
    nsIDocument *mDocument;
    nsTArray<nsAutoPtr<MutationRecord> > mPendingMutations;
    nsIContent *mCurrentTarget;
    nsClassHashtable<nsPtrHashKey<nsIContent>, observer_array_type> mContentToObserverTable;

    static uint32_t sInhibitDepth;
};

/* This class is intended to be used inside GObject signal handlers.
 * The idea is that we prevent our view of the menus from mutating
 * whilst dispatching events in to Gecko.
 */
class nsNativeMenuAutoSuspendMutations
{
public:
    nsNativeMenuAutoSuspendMutations() {
        nsNativeMenuDocListener::AddMutationBlocker();
    }

    ~nsNativeMenuAutoSuspendMutations() {
        nsNativeMenuDocListener::RemoveMutationBlocker();
    }
};

#endif /* __nsNativeMenuDocListener_h__ */

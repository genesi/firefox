/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNativeMenuDocListener.h"

#include "nsIDocument.h"
#include "mozilla/dom/Element.h"
#include "nsINode.h"
#include "mozilla/DebugOnly.h"

#include "nsMenuObject.h"

using namespace mozilla;

uint32_t nsNativeMenuDocListener::sUpdateDepth = 0;

nsNativeMenuDocListener::listener_array_type *gPendingListeners;

/*
 * Small helper which caches a single listener, so that consecutive
 * events which go to the same node avoid multiple hash table lookups
 */
class DispatchHelper {
public:
    DispatchHelper(nsNativeMenuDocListener *aListener,
                   nsIContent *aContent
                   MOZ_GUARD_OBJECT_NOTIFIER_PARAM) :
                   mObserver(nullptr) {
        MOZ_GUARD_OBJECT_NOTIFIER_INIT;
        if (aContent == aListener->mLastSource) {
            mObserver = aListener->mLastTarget;
        } else {
            mObserver = aListener->mContentToObserverTable.Get(aContent);
            if (mObserver) {
                aListener->mLastSource = aContent;
                aListener->mLastTarget = mObserver;
            }
        }
    }

    ~DispatchHelper() { };

    nsNativeMenuChangeObserver* Observer() const { return mObserver; }

    bool HasObserver() const { return !!mObserver; }

private:
    nsNativeMenuChangeObserver *mObserver;
    MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER
};

NS_IMPL_ISUPPORTS1(nsNativeMenuDocListener, nsIMutationObserver)

void
nsNativeMenuDocListener::DoAttributeChanged(nsIContent *aContent,
                                            nsIAtom *aAttribute)
{
    DispatchHelper h(this, aContent);
    if (h.HasObserver()) {
        h.Observer()->OnAttributeChanged(aContent, aAttribute);
    }
}

void
nsNativeMenuDocListener::DoContentInserted(nsIContent *aContainer,
                                           nsIContent *aChild,
                                           nsIContent *aPrevSibling)
{
    DispatchHelper h(this, aContainer);
    if (h.HasObserver()) {
        h.Observer()->OnContentInserted(aContainer, aChild, aPrevSibling);
    }
}

void
nsNativeMenuDocListener::DoContentRemoved(nsIContent *aContainer,
                                          nsIContent *aChild)
{
    DispatchHelper h(this, aContainer);
    if (h.HasObserver()) {
        h.Observer()->OnContentRemoved(aContainer, aChild);
    }
}

void
nsNativeMenuDocListener::DoBeginUpdateBatch(nsIContent *aTarget)
{
    DispatchHelper h(this, aTarget);
    if (h.HasObserver()) {
        h.Observer()->BeginUpdateBatch(aTarget);
    }
}

void
nsNativeMenuDocListener::DoEndUpdateBatch(nsIContent *aTarget)
{
    DispatchHelper h(this, aTarget);
    if (h.HasObserver()) {
        h.Observer()->EndUpdateBatch();
    }
}

void
nsNativeMenuDocListener::FlushPendingMutations()
{
    nsIContent *batchTarget = nullptr;
    bool inUpdateBatch = false;

    while (mPendingMutations.Length() > 0) {
        MutationRecord *m = mPendingMutations[0];

        if (m->mTarget != batchTarget) {
            if (inUpdateBatch) {
                DoEndUpdateBatch(batchTarget);
                inUpdateBatch = false;
            }

            batchTarget = m->mTarget;

            if (mPendingMutations.Length() > 1 &&
                mPendingMutations[1]->mTarget == batchTarget) {
                DoBeginUpdateBatch(batchTarget);
                inUpdateBatch = true;
            }
        }

        switch (m->mType) {
            case MutationRecord::eAttributeChanged:
                DoAttributeChanged(m->mTarget, m->mAttribute);
                break;
            case MutationRecord::eContentInserted:
                DoContentInserted(m->mTarget, m->mChild, m->mPrevSibling);
                break;
            case MutationRecord::eContentRemoved:
                DoContentRemoved(m->mTarget, m->mChild);
                break;
            default:
                NS_NOTREACHED("Invalid type");
        }

        mPendingMutations.RemoveElementAt(0);
    }

    if (inUpdateBatch) {
        DoEndUpdateBatch(batchTarget);
    }
}

/* static */ void
nsNativeMenuDocListener::ScheduleFlush(nsNativeMenuDocListener *aListener)
{
    NS_ASSERTION(sUpdateDepth > 0, "Shouldn't be doing this now");

    if (!gPendingListeners) {
        gPendingListeners = new listener_array_type;
    }

    if (gPendingListeners->IndexOf(aListener) ==
        listener_array_type::NoIndex) {
        gPendingListeners->AppendElement(aListener);
    }
}

/* static */ void
nsNativeMenuDocListener::UnscheduleFlush(nsNativeMenuDocListener *aListener)
{
    if (!gPendingListeners) {
        return;
    }

    gPendingListeners->RemoveElement(aListener);
}

/* static */ void
nsNativeMenuDocListener::EndUpdates()
{
    if (sUpdateDepth == 1 && gPendingListeners) {
        {
            while (gPendingListeners->Length() > 0) {
                (*gPendingListeners)[0]->FlushPendingMutations();
                gPendingListeners->RemoveElementAt(0);
            }
        }

        delete gPendingListeners;
        gPendingListeners = nullptr;
    }
 
    NS_ASSERTION(sUpdateDepth > 0, "Negative update depth!");
    sUpdateDepth--;
}

nsNativeMenuDocListener::nsNativeMenuDocListener() :
    mDocument(nullptr),
    mLastSource(nullptr),
    mLastTarget(nullptr)
{
    MOZ_COUNT_CTOR(nsNativeMenuDocListener);
    mContentToObserverTable.Init();
}

nsNativeMenuDocListener::~nsNativeMenuDocListener()
{
    NS_ASSERTION(mContentToObserverTable.Count() == 0,
                 "Some nodes forgot to unregister listeners");
    UnscheduleFlush(this);
    MOZ_COUNT_DTOR(nsNativeMenuDocListener);
}

nsresult
nsNativeMenuDocListener::Init(nsIContent *aRootNode)
{
    NS_ENSURE_ARG(aRootNode);

    mRootNode = aRootNode;

    return NS_OK;
}

void
nsNativeMenuDocListener::AttributeChanged(nsIDocument *aDocument,
                                          mozilla::dom::Element *aElement,
                                          int32_t aNameSpaceID,
                                          nsIAtom *aAttribute,
                                          int32_t aModType)
{
    if (sUpdateDepth == 0) {
        DoAttributeChanged(aElement, aAttribute);
        return;
    }

    MutationRecord *m = *mPendingMutations.AppendElement(new MutationRecord);
    m->mType = MutationRecord::eAttributeChanged;
    m->mTarget = aElement;
    m->mAttribute = aAttribute;

    ScheduleFlush(this);
}

void
nsNativeMenuDocListener::ContentAppended(nsIDocument *aDocument,
                                         nsIContent *aContainer,
                                         nsIContent *aFirstNewContent,
                                         int32_t aNewIndexInContainer)
{
    for (nsIContent *c = aFirstNewContent; c; c = c->GetNextSibling()) {
        ContentInserted(aDocument, aContainer, c, 0);
    }
}

void
nsNativeMenuDocListener::ContentInserted(nsIDocument *aDocument,
                                         nsIContent *aContainer,
                                         nsIContent *aChild,
                                         int32_t aIndexInContainer)
{
    nsIContent *prevSibling = nsMenuObjectContainer::GetPreviousSupportedSibling(aChild);

    if (sUpdateDepth == 0) {
        DoContentInserted(aContainer, aChild, prevSibling);
        return;
    }

    MutationRecord *m = *mPendingMutations.AppendElement(new MutationRecord);
    m->mType = MutationRecord::eContentInserted;
    m->mTarget = aContainer;
    m->mChild = aChild;
    m->mPrevSibling = prevSibling;

    ScheduleFlush(this);
}

void
nsNativeMenuDocListener::ContentRemoved(nsIDocument *aDocument,
                                        nsIContent *aContainer,
                                        nsIContent *aChild,
                                        int32_t aIndexInContainer,
                                        nsIContent *aPreviousSibling)
{
    if (sUpdateDepth == 0) {
        DoContentRemoved(aContainer, aChild);
        return;
    }

    MutationRecord *m = *mPendingMutations.AppendElement(new MutationRecord);
    m->mType = MutationRecord::eContentRemoved;
    m->mTarget = aContainer;
    m->mChild = aChild;

    ScheduleFlush(this);
}                                                           

void
nsNativeMenuDocListener::NodeWillBeDestroyed(const nsINode *aNode)
{
    mDocument = nullptr;
}

/* static */ already_AddRefed<nsNativeMenuDocListener>
nsNativeMenuDocListener::Create(nsIContent *aRootNode)
{
    nsRefPtr<nsNativeMenuDocListener> listener = new nsNativeMenuDocListener();
    if (NS_FAILED(listener->Init(aRootNode))) {
        return nullptr;
    }

    return listener.forget();
}

void
nsNativeMenuDocListener::RegisterForContentChanges(nsIContent *aContent,
                                                   nsNativeMenuChangeObserver *aObserver)
{
    NS_ASSERTION(aContent, "Need content parameter");
    NS_ASSERTION(aObserver, "Need observer parameter");
    if (!aContent || !aObserver) {
        return;
    }

    DebugOnly<nsNativeMenuChangeObserver *> old;
    NS_ASSERTION(!mContentToObserverTable.Get(aContent, &old) || old == aObserver,
                 "Multiple observers for the same content node are not supported");

    mContentToObserverTable.Put(aContent, aObserver);
}

void
nsNativeMenuDocListener::UnregisterForContentChanges(nsIContent *aContent)
{
    NS_ASSERTION(aContent, "Need content parameter");
    if (!aContent) {
        return;
    }

    mContentToObserverTable.Remove(aContent);
    if (aContent == mLastSource) {
        mLastSource = nullptr;
        mLastTarget = nullptr;
    }
}

void
nsNativeMenuDocListener::Start()
{
    if (mDocument) {
        return;
    }

    mDocument = mRootNode->OwnerDoc();
    if (!mDocument) {
        return;
    }

    mDocument->AddMutationObserver(this);
}

void
nsNativeMenuDocListener::Stop()
{
    if (mDocument) {
        mDocument->RemoveMutationObserver(this);
        mDocument = nullptr;
    }

    UnscheduleFlush(this);
    mPendingMutations.Clear();
}

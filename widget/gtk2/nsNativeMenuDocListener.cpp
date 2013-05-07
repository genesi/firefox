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

#include "nsMenuObject.h"

uint32_t nsNativeMenuDocListener::sInhibitDepth = 0;

nsNativeMenuDocListener::listener_array_type *gPendingListeners;

class DispatchContext {
public:
    DispatchContext(nsNativeMenuDocListener *aOwner,
                    nsIContent *aTarget) :
        mOwner(aOwner), mObservers(nullptr) {
        NS_ASSERTION(!mOwner->mCurrentTarget,
                     "Nested dispatch! This should never happen");
        mOwner->mCurrentTarget = aTarget;
        mOwner->mContentToObserverTable.Get(aTarget, &mObservers);
    };

    ~DispatchContext() {
        if (mObservers && mObservers->Length() == 0) {
            mOwner->mContentToObserverTable.Remove(mOwner->mCurrentTarget);
        }

        mOwner->mCurrentTarget = nullptr;
    };

    bool HasObservers() const {
        return mObservers ? true : false;
    };

    nsNativeMenuDocListener::observer_array_type& Observers() const {
        return *mObservers;
    };

private:
    nsNativeMenuDocListener *mOwner;
    nsNativeMenuDocListener::observer_array_type *mObservers;
};

NS_IMPL_ISUPPORTS1(nsNativeMenuDocListener, nsIMutationObserver)

void
nsNativeMenuDocListener::DoAttributeChanged(nsIContent *aContent,
                                            nsIAtom *aAttribute)
{
    DispatchContext ctx(this, aContent);

    if (ctx.HasObservers()) {
        observer_array_type::ForwardIterator iter(ctx.Observers());
        while (iter.HasMore()) {
            iter.GetNext()->OnAttributeChanged(aContent, aAttribute);
        }
    }
}

void
nsNativeMenuDocListener::DoContentInserted(nsIContent *aContainer,
                                           nsIContent *aChild,
                                           nsIContent *aPrevSibling)
{
    DispatchContext ctx(this, aContainer);

    if (ctx.HasObservers()) {
        observer_array_type::ForwardIterator iter(ctx.Observers());
        while (iter.HasMore()) {
            iter.GetNext()->OnContentInserted(aContainer, aChild, aPrevSibling);
        }
    }
}

void
nsNativeMenuDocListener::DoContentRemoved(nsIContent *aContainer,
                                          nsIContent *aChild)
{
    DispatchContext ctx(this, aContainer);

    if (ctx.HasObservers()) {
        observer_array_type::ForwardIterator iter(ctx.Observers());
        while (iter.HasMore()) {
            iter.GetNext()->OnContentRemoved(aContainer, aChild);
        }
    }
}

void
nsNativeMenuDocListener::DoBeginUpdateBatch(nsIContent *aTarget)
{
    DispatchContext ctx(this, aTarget);

    if (ctx.HasObservers()) {
        observer_array_type::ForwardIterator iter(ctx.Observers());
        while (iter.HasMore()) {
            iter.GetNext()->BeginUpdateBatch(aTarget);
        }
    }
}

void
nsNativeMenuDocListener::DoEndUpdateBatch(nsIContent *aTarget)
{
    DispatchContext ctx(this, aTarget);

    if (ctx.HasObservers()) {
        observer_array_type::ForwardIterator iter(ctx.Observers());
        while (iter.HasMore()) {
            iter.GetNext()->EndUpdateBatch();
        }
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
nsNativeMenuDocListener::ScheduleListener(nsNativeMenuDocListener *aListener)
{
    NS_ASSERTION(sInhibitDepth > 0, "Shouldn't be doing this now");

    if (!gPendingListeners) {
        gPendingListeners = new listener_array_type;
    }

    if (gPendingListeners->IndexOf(aListener) ==
        listener_array_type::NoIndex) {
        gPendingListeners->AppendElement(aListener);
    }
}

/* static */ void
nsNativeMenuDocListener::RemoveListener(nsNativeMenuDocListener *aListener)
{
    if (!gPendingListeners) {
        return;
    }

    gPendingListeners->RemoveElement(aListener);
}

/* static */ void
nsNativeMenuDocListener::RemoveMutationBlocker()
{
    if (sInhibitDepth == 1 && gPendingListeners) {
        {
            while (gPendingListeners->Length() > 0) {
                (*gPendingListeners)[0]->FlushPendingMutations();
                gPendingListeners->RemoveElementAt(0);
            }
        }

        delete gPendingListeners;
        gPendingListeners = nullptr;
    }
 
    NS_ASSERTION(sInhibitDepth > 0, "Negative inhibit depth!");
    sInhibitDepth--;
}

nsNativeMenuDocListener::nsNativeMenuDocListener() :
    mDocument(nullptr),
    mCurrentTarget(nullptr)
{
    MOZ_COUNT_CTOR(nsNativeMenuDocListener);
    mContentToObserverTable.Init();
}

nsNativeMenuDocListener::~nsNativeMenuDocListener()
{
    NS_ASSERTION(mContentToObserverTable.Count() == 0,
                 "Some nodes forgot to unregister listeners");
    RemoveListener(this);
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
    if (sInhibitDepth == 0) {
        DoAttributeChanged(aElement, aAttribute);
        return;
    }

    MutationRecord *m = *mPendingMutations.AppendElement(new MutationRecord);
    m->mType = MutationRecord::eAttributeChanged;
    m->mTarget = aElement;
    m->mAttribute = aAttribute;

    ScheduleListener(this);
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

    if (sInhibitDepth == 0) {
        DoContentInserted(aContainer, aChild, prevSibling);
        return;
    }

    MutationRecord *m = *mPendingMutations.AppendElement(new MutationRecord);
    m->mType = MutationRecord::eContentInserted;
    m->mTarget = aContainer;
    m->mChild = aChild;
    m->mPrevSibling = prevSibling;

    ScheduleListener(this);
}

void
nsNativeMenuDocListener::ContentRemoved(nsIDocument *aDocument,
                                        nsIContent *aContainer,
                                        nsIContent *aChild,
                                        int32_t aIndexInContainer,
                                        nsIContent *aPreviousSibling)
{
    if (sInhibitDepth == 0) {
        DoContentRemoved(aContainer, aChild);
        return;
    }

    MutationRecord *m = *mPendingMutations.AppendElement(new MutationRecord);
    m->mType = MutationRecord::eContentRemoved;
    m->mTarget = aContainer;
    m->mChild = aChild;

    ScheduleListener(this);
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
                                                   nsMenuObject *aMenuObject)
{
    NS_ASSERTION(aContent && aMenuObject, "Invalid parameters");
    if (!aContent || !aMenuObject) {
        return;
    }

    nsTObserverArray<nsMenuObject *> *observers;
    if (!mContentToObserverTable.Get(aContent, &observers)) {
        observers = new nsTObserverArray<nsMenuObject *>;
        mContentToObserverTable.Put(aContent, observers);
    }

    observers->AppendElement(aMenuObject);
}

void
nsNativeMenuDocListener::UnregisterForContentChanges(nsIContent *aContent,
                                                     nsMenuObject *aMenuObject)
{
    NS_ASSERTION(aContent && aMenuObject, "Invalid parameters");
    if (!aContent || !aMenuObject) {
        return;
    }

    nsTObserverArray<nsMenuObject *> *observers;
    if (!mContentToObserverTable.Get(aContent, &observers)) {
        return;
    }

    observers->RemoveElement(aMenuObject);

    /* We can't remove the observer array here if we are currently
     * dispatching events for this content node, as it will
     * crash inside the iterators destructor. DispatchContext
     * takes care of removing this for us once we have returned from
     * this dispatch
     */
    if (observers->Length() == 0 && aContent != mCurrentTarget) {
        mContentToObserverTable.Remove(aContent);
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

    RemoveListener(this);
    mPendingMutations.Clear();
}

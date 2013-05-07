/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsNativeMenuService_h__
#define __nsNativeMenuService_h__

#include "nsTArray.h"
#include "nsIObserver.h"
#include "nsINativeMenuService.h"
#include "nsCOMPtr.h"
#include "mozilla/Attributes.h"

#include "nsNativeMenuUtils.h"

#include <gio/gio.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

class nsMenuBar;

/*
 * The main native menu service singleton. nsWebShellWindow calls in to this when
 * a new top level window is created.
 *
 * Menubars are owned by their nsWindow. This service holds a weak reference to
 * each menubar for the purpose of re-registering them with the shell if it
 * needs to. The menubar is responsible for notifying the service when the last
 * reference to it is dropped.
 */
class nsNativeMenuService MOZ_FINAL : public nsINativeMenuService,
                                      public nsIObserver
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER

    NS_IMETHOD CreateNativeMenuBar(nsIWidget* aParent, nsIContent* aMenuBarNode);

    nsresult Init();

    // Returns the singleton addref'd for the service manager
    static already_AddRefed<nsNativeMenuService> GetInstance();

    // Returns the singleton without increasing the reference count
    static nsNativeMenuService* GetSingleton();

    // Called by a menubar when the last reference to it is dropped
    void NotifyNativeMenuBarDestroyed(nsMenuBar *aMenuBar);

private:
    nsNativeMenuService() :
        mDbusProxy(nullptr),
        mOnline(false) { };
    ~nsNativeMenuService();

    static void EnsureInitialized();
    void SetOnline(bool aOnline);
    void RegisterNativeMenuBar(nsMenuBar *aMenuBar);
    static void name_owner_changed_cb(GObject *gobject,
                                      GParamSpec *pspec,
                                      gpointer user_data);
    static void proxy_created_cb(GObject *source_object,
                                 GAsyncResult *res,
                                 gpointer user_data);
    static void register_native_menubar_cb(GObject *source_object,
                                           GAsyncResult *res,
                                           gpointer user_data);
    static gboolean map_event_cb(GtkWidget *widget, GdkEvent *event,
                                 gpointer user_data);
    void OnNameOwnerChanged();
    void OnProxyCreated(GDBusProxy *aProxy);
    void OnNativeMenuBarRegistered(nsMenuBar *aMenuBar,
                                   bool aSuccess);
    static int PrefChangedCallback(const char *aPref, void *aClosure);
    void PrefChanged();

    nsNativeMenuGIORequest mCreateProxyRequest;
    GDBusProxy *mDbusProxy;
    bool mOnline;
    nsTArray<nsMenuBar *> mMenuBars;

    static bool sShutdown;
    static nsNativeMenuService *sService;
};

#endif /* __nsNativeMenuService_h__ */

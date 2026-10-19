#include "SystemTools.hpp"

#include <Foundation/PlatformDetection.hpp>

#ifdef PLATFORM_LINUX

#include <Foundation/Logger.hpp>
#include <gtk/gtk.h>

namespace Panda {

SystemTools::SaveChangesChoice SystemTools::showSaveChangesDialog(const char *title, const char *message) {
    if (!gtk_init_check(nullptr, nullptr)) {
        LOG_ERROR("Failed to initialize GTK for the save changes dialog");
        return SaveChangesChoice::Cancel;
    }

    // The question may contain a file name: pass it as plain text, never as markup or a format.
    GtkWidget *dialog = gtk_message_dialog_new(
        nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "%s", title ? title : ""
    );
    if (!dialog) {
        LOG_ERROR("Failed to create the save changes dialog");
        return SaveChangesChoice::Cancel;
    }
    // Keep the widget alive until cleanup even if GTK destroys it during the nested loop.
    g_object_ref_sink(dialog);
    gtk_window_set_title(GTK_WINDOW(dialog), "Unsaved Changes");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message ? message : "");
    gtk_dialog_add_buttons(
        GTK_DIALOG(dialog), "_Don't Save", GTK_RESPONSE_REJECT, "_Cancel", GTK_RESPONSE_CANCEL, "_Save",
        GTK_RESPONSE_ACCEPT, nullptr
    );
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    // Finish native window teardown before the editor resumes its close flow.
    while (gtk_events_pending()) { gtk_main_iteration(); }

    if (response == GTK_RESPONSE_ACCEPT) { return SaveChangesChoice::Save; }
    if (response == GTK_RESPONSE_REJECT) { return SaveChangesChoice::DontSave; }
    // Escape, window close, destruction and every other response preserve the document.
    return SaveChangesChoice::Cancel;
}

} // namespace Panda

#endif

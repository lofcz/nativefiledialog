/*
  Native File Dialog

  http://www.frogtoss.com/labs
*/

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <gtk/gtk.h>
#include "nfd.h"
#include "nfd_common.h"
#include "common.h"



const char INIT_FAIL_MSG[] = "gtk_init_check failed to initilaize GTK+";


static void AddTypeToFilterName( const char *typebuf, char *filterName, size_t bufsize )
{
    const char SEP[] = ", ";

    size_t len = strlen(filterName);
    if ( len != 0 )
    {
        strncat( filterName, SEP, bufsize - len - 1 );
        len += strlen(SEP);
    }
    
    strncat( filterName, typebuf, bufsize - len - 1 );
}

static void AddFiltersToDialog(GtkWidget *dialog, const char *filterList)
{
    NFDFilterList filters;

    // Použijeme společnou funkci pro parsování filtrů
    if (!ParseFilterList(filterList, &filters)) {
        return;
    }

    // Přidáme každý filtr
    for (size_t i = 0; i < filters.count; ++i) {
        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, filters.filters[i].name);

        // Rozdělíme pattern podle středníků a přidáme každý vzor
        char* pattern = strdup(filters.filters[i].pattern);
        if (pattern) {
            char* saveptr = NULL;
            char* token = strtok_r(pattern, ";", &saveptr);

            while (token) {
                // Ořežeme whitespace
                while (*token == ' ') token++;
                char* end = token + strlen(token) - 1;
                while (end > token && *end == ' ') *end-- = '\0';

                if (*token) {
                    gtk_file_filter_add_pattern(filter, token);
                }
                token = strtok_r(NULL, ";", &saveptr);
            }

            free(pattern);
        }

        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    }

    // Přidáme wildcard filtr na konec
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "*.*");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    // Cleanup
    NFDi_Free(filters.filters);
}

static void SetDefaultPath( GtkWidget *dialog, const char *defaultPath )
{
    if ( !defaultPath || strlen(defaultPath) == 0 )
        return;

    /* GTK+ manual recommends not specifically setting the default path.
       We do it anyway in order to be consistent across platforms.

       If consistency with the native OS is preferred, this is the line
       to comment out. -ml */
    gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER(dialog), defaultPath );
}

static nfdresult_t AllocPathSet( GSList *fileList, nfdpathset_t *pathSet )
{
    size_t bufSize = 0;
    GSList *node;
    nfdchar_t *p_buf;
    size_t count = 0;
    
    assert(fileList);
    assert(pathSet);

    pathSet->count = (size_t)g_slist_length( fileList );
    assert( pathSet->count > 0 );

    pathSet->indices = NFDi_Malloc( sizeof(size_t)*pathSet->count );
    if ( !pathSet->indices )
    {
        return NFD_ERROR;
    }

    /* count the total space needed for buf */
    for ( node = fileList; node; node = node->next )
    {
        assert(node->data);
        bufSize += strlen( (const gchar*)node->data ) + 1;
    }

    pathSet->buf = NFDi_Malloc( sizeof(nfdchar_t) * bufSize );

    /* fill buf */
    p_buf = pathSet->buf;
    for ( node = fileList; node; node = node->next )
    {
        nfdchar_t *path = (nfdchar_t*)(node->data);
        size_t byteLen = strlen(path)+1;
        ptrdiff_t index;
        
        memcpy( p_buf, path, byteLen );
        g_free(node->data);

        index = p_buf - pathSet->buf;
        assert( index >= 0 );
        pathSet->indices[count] = (size_t)index;

        p_buf += byteLen;
        ++count;
    }

    g_slist_free( fileList );
    
    return NFD_OKAY;
}

static void WaitForCleanup(void)
{
    while (gtk_events_pending())
        gtk_main_iteration();
}
                                 
/* public */

nfdresult_t NFD_OpenDialogEx(
    const nfdchar_t *filterList,
    const nfdchar_t *defaultPath,
    const nfdchar_t *dialogTitle,
    const nfdchar_t *fileNameLabel,
    const nfdchar_t *selectButtonLabel,
    const nfdchar_t *cancelButtonLabel,
    nfdchar_t **outPath )
{
    GtkWidget *dialog;
    nfdresult_t result;

    if ( !gtk_init_check( NULL, NULL ) )
    {
        NFDi_SetError(INIT_FAIL_MSG);
        return NFD_ERROR;
    }

    dialog = gtk_file_chooser_dialog_new( dialogTitle ? dialogTitle : "Open File",
                                          NULL,
                                          GTK_FILE_CHOOSER_ACTION_OPEN,
                                          cancelButtonLabel ? cancelButtonLabel : "_Cancel",
                                          GTK_RESPONSE_CANCEL,
                                          selectButtonLabel ? selectButtonLabel : "_Open",
                                          GTK_RESPONSE_ACCEPT,
                                          NULL );

    if (fileNameLabel) {
        GtkWidget *label = gtk_label_new(fileNameLabel);
        gtk_widget_show(label);
        gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), label);
    }

    /* Build the filter list */
    AddFiltersToDialog(dialog, filterList);

    /* Set the default path */
    SetDefaultPath(dialog, defaultPath);

    result = NFD_CANCEL;
    if ( gtk_dialog_run( GTK_DIALOG(dialog) ) == GTK_RESPONSE_ACCEPT )
    {
        char *filename;

        filename = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER(dialog) );

        {
            size_t len = strlen(filename);
            *outPath = NFDi_Malloc( len + 1 );
            memcpy( *outPath, filename, len + 1 );
            if ( !*outPath )
            {
                g_free( filename );
                gtk_widget_destroy(dialog);
                return NFD_ERROR;
            }
        }
        g_free( filename );

        result = NFD_OKAY;
    }

    WaitForCleanup();
    gtk_widget_destroy(dialog);
    WaitForCleanup();

    return result;
}


nfdresult_t NFD_OpenDialog( const nfdchar_t *filterList,
                            const nfdchar_t *defaultPath,
                            nfdchar_t **outPath )
{    
    GtkWidget *dialog;
    nfdresult_t result;

    if ( !gtk_init_check( NULL, NULL ) )
    {
        NFDi_SetError(INIT_FAIL_MSG);
        return NFD_ERROR;
    }

    dialog = gtk_file_chooser_dialog_new( "Open File",
                                          NULL,
                                          GTK_FILE_CHOOSER_ACTION_OPEN,
                                          "_Cancel", GTK_RESPONSE_CANCEL,
                                          "_Open", GTK_RESPONSE_ACCEPT,
                                          NULL );

    /* Build the filter list */
    AddFiltersToDialog(dialog, filterList);

    /* Set the default path */
    SetDefaultPath(dialog, defaultPath);

    result = NFD_CANCEL;
    if ( gtk_dialog_run( GTK_DIALOG(dialog) ) == GTK_RESPONSE_ACCEPT )
    {
        char *filename;

        filename = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER(dialog) );

        {
            size_t len = strlen(filename);
            *outPath = NFDi_Malloc( len + 1 );
            memcpy( *outPath, filename, len + 1 );
            if ( !*outPath )
            {
                g_free( filename );
                gtk_widget_destroy(dialog);
                return NFD_ERROR;
            }
        }
        g_free( filename );

        result = NFD_OKAY;
    }

    WaitForCleanup();
    gtk_widget_destroy(dialog);
    WaitForCleanup();

    return result;
}


nfdresult_t NFD_OpenDialogMultiple( const nfdchar_t *filterList,
                                    const nfdchar_t *defaultPath,
                                    nfdpathset_t *outPaths )
{
    GtkWidget *dialog;
    nfdresult_t result;

    if ( !gtk_init_check( NULL, NULL ) )
    {
        NFDi_SetError(INIT_FAIL_MSG);
        return NFD_ERROR;
    }

    dialog = gtk_file_chooser_dialog_new( "Open Files",
                                          NULL,
                                          GTK_FILE_CHOOSER_ACTION_OPEN,
                                          "_Cancel", GTK_RESPONSE_CANCEL,
                                          "_Open", GTK_RESPONSE_ACCEPT,
                                          NULL );
    gtk_file_chooser_set_select_multiple( GTK_FILE_CHOOSER(dialog), TRUE );

    /* Build the filter list */
    AddFiltersToDialog(dialog, filterList);

    /* Set the default path */
    SetDefaultPath(dialog, defaultPath);

    result = NFD_CANCEL;
    if ( gtk_dialog_run( GTK_DIALOG(dialog) ) == GTK_RESPONSE_ACCEPT )
    {
        GSList *fileList = gtk_file_chooser_get_filenames( GTK_FILE_CHOOSER(dialog) );
        if ( AllocPathSet( fileList, outPaths ) == NFD_ERROR )
        {
            gtk_widget_destroy(dialog);
            return NFD_ERROR;
        }
        
        result = NFD_OKAY;
    }

    WaitForCleanup();
    gtk_widget_destroy(dialog);
    WaitForCleanup();

    return result;
}

nfdresult_t NFD_SaveDialog( const nfdchar_t *filterList,
                            const nfdchar_t *defaultPath,
                            nfdchar_t **outPath )
{
    GtkWidget *dialog;
    nfdresult_t result;

    if ( !gtk_init_check( NULL, NULL ) )
    {
        NFDi_SetError(INIT_FAIL_MSG);
        return NFD_ERROR;
    }

    dialog = gtk_file_chooser_dialog_new( "Save File",
                                          NULL,
                                          GTK_FILE_CHOOSER_ACTION_SAVE,
                                          "_Cancel", GTK_RESPONSE_CANCEL,
                                          "_Save", GTK_RESPONSE_ACCEPT,
                                          NULL ); 
    gtk_file_chooser_set_do_overwrite_confirmation( GTK_FILE_CHOOSER(dialog), TRUE );

    /* Build the filter list */    
    AddFiltersToDialog(dialog, filterList);

    /* Set the default path */
    SetDefaultPath(dialog, defaultPath);
    
    result = NFD_CANCEL;    
    if ( gtk_dialog_run( GTK_DIALOG(dialog) ) == GTK_RESPONSE_ACCEPT )
    {
        char *filename;
        filename = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER(dialog) );
        
        {
            size_t len = strlen(filename);
            *outPath = NFDi_Malloc( len + 1 );
            memcpy( *outPath, filename, len + 1 );
            if ( !*outPath )
            {
                g_free( filename );
                gtk_widget_destroy(dialog);
                return NFD_ERROR;
            }
        }
        g_free(filename);

        result = NFD_OKAY;
    }

    WaitForCleanup();
    gtk_widget_destroy(dialog);
    WaitForCleanup();
    
    return result;
}

nfdresult_t NFD_PickFolder(const nfdchar_t *defaultPath,
    nfdchar_t **outPath)
{
    GtkWidget *dialog;
    nfdresult_t result;

    if (!gtk_init_check(NULL, NULL))
    {
        NFDi_SetError(INIT_FAIL_MSG);
        return NFD_ERROR;
    }

    dialog = gtk_file_chooser_dialog_new( "Select folder",
                                          NULL,
                                          GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                          "_Cancel", GTK_RESPONSE_CANCEL,
                                          "_Select", GTK_RESPONSE_ACCEPT,
                                          NULL ); 
    gtk_file_chooser_set_do_overwrite_confirmation( GTK_FILE_CHOOSER(dialog), TRUE );


    /* Set the default path */
    SetDefaultPath(dialog, defaultPath);
    
    result = NFD_CANCEL;    
    if ( gtk_dialog_run( GTK_DIALOG(dialog) ) == GTK_RESPONSE_ACCEPT )
    {
        char *filename;
        filename = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER(dialog) );
        
        {
            size_t len = strlen(filename);
            *outPath = NFDi_Malloc( len + 1 );
            memcpy( *outPath, filename, len + 1 );
            if ( !*outPath )
            {
                g_free( filename );
                gtk_widget_destroy(dialog);
                return NFD_ERROR;
            }
        }
        g_free(filename);

        result = NFD_OKAY;
    }

    WaitForCleanup();
    gtk_widget_destroy(dialog);
    WaitForCleanup();
    
    return result;
}

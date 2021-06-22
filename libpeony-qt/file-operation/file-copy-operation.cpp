/*
 * Peony-Qt's Library
 *
 * Copyright (C) 2020, KylinSoft Co., Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors: Yue Lan <lanyue@kylinos.cn>
 *
 */

#include "file-copy-operation.h"

#include "file-node-reporter.h"
#include "file-node.h"
#include "file-enumerator.h"
#include "file-info.h"

#include "file-utils.h"

#include "file-operation-manager.h"

#include "clipboard-utils.h"
#include <QProcess>
#include <QDebug>
#include <file-copy.h>

using namespace Peony;

static void handleDuplicate(FileNode *node)
{
    node->setDestFileName(FileUtils::handleDuplicateName(node->destBaseName()));
}

FileCopyOperation::FileCopyOperation(QStringList sourceUris, QString destDirUri, QObject *parent) : FileOperation (parent)
{
    QUrl destDirUrl = destDirUri;
    QUrl firstSrcUrl = sourceUris.first();
    if (destDirUrl.isParentOf(firstSrcUrl)) {
        m_is_duplicated_copy = true;
    } else {
        auto lastPasteDirectoryUri = ClipboardUtils::getInstance()->getLastTargetDirectoryUri();
        QUrl lastPasteDirectoryUrl = lastPasteDirectoryUri;
        if (destDirUrl == lastPasteDirectoryUrl) {
            m_is_duplicated_copy = true;
        }
    }

    m_conflict_files.clear();
    m_source_uris = sourceUris;
    m_dest_dir_uri = FileUtils::urlDecode(destDirUri);
    m_reporter = new FileNodeReporter;
    connect(m_reporter, &FileNodeReporter::nodeFound, this, &FileOperation::operationPreparedOne);

    m_info = std::make_shared<FileOperationInfo>(sourceUris, destDirUri, FileOperationInfo::Copy);
}

FileCopyOperation::~FileCopyOperation()
{
    delete m_reporter;
    m_conflict_files.clear();
}

ExceptionResponse FileCopyOperation::prehandle(GError *err)
{
    setHasError(true);

    switch (err->code) {
        case G_IO_ERROR_BUSY:
        case G_IO_ERROR_PENDING:
        case G_IO_ERROR_NO_SPACE:
        case G_IO_ERROR_CANCELLED:
        case G_IO_ERROR_INVALID_DATA:
        case G_IO_ERROR_NOT_SUPPORTED:
        case G_IO_ERROR_PERMISSION_DENIED:
        case G_IO_ERROR_CANT_CREATE_BACKUP:
        case G_IO_ERROR_TOO_MANY_OPEN_FILES:
            return Other;
    }

    if (G_IO_ERROR_EXISTS == err->code && m_is_duplicated_copy) {
        return BackupAll;
    }

    if (m_prehandle_hash.contains(err->code))
        return m_prehandle_hash.value(err->code);

    return Other;
}

void FileCopyOperation::progress_callback(goffset current_num_bytes,
        goffset total_num_bytes,
        FileCopyOperation *p_this)
{
    if (total_num_bytes < current_num_bytes)
        return;

    QUrl url(p_this->m_current_src_uri);
    auto currnet = p_this->m_current_offset + current_num_bytes;
    auto total = p_this->m_total_szie;
    auto fileIconName = FileUtils::getFileIconName(p_this->m_current_src_uri, false);
    auto destFileName = FileUtils::isFileDirectory(p_this->m_current_dest_dir_uri) ?
                p_this->m_current_dest_dir_uri + "/" + url.fileName() : p_this->m_current_dest_dir_uri;
//    qDebug()<<currnet*1.0/total;
    Q_EMIT p_this->FileProgressCallback(p_this->m_current_src_uri, destFileName, fileIconName, currnet, total);
}

void FileCopyOperation::copyRecursively(FileNode *node)
{
    if (isCancelled())
        return;

    node->setState(FileNode::Handling);
    QString destName = "";

fallback_retry:
    QString destFileUri = node->resolveDestFileUri(m_dest_dir_uri);
    QUrl destFileUrl = destFileUri;
    node->setDestUri(destFileUri);
    QString srcUri = node->uri();
    qDebug()<<"dest file uri:"<<destFileUri;

    GFileWrapperPtr destFile = wrapGFile(g_file_new_for_uri(destFileUri.toUtf8().constData()));

    m_current_src_uri = node->uri();
    m_current_dest_dir_uri = destFileUri;

    if (node->isFolder()) {
        GError *err = nullptr;

        //NOTE: mkdir doesn't have a progress callback.
        g_file_make_directory(destFile.get()->get(),
                              getCancellable().get()->get(),
                              &err);
        if (err) {
            FileOperationError except;
            if (err->code == G_IO_ERROR_CANCELLED) {
                return;
            }
            auto errWrapperPtr = GErrorWrapper::wrapFrom(err);
            int handle_type = prehandle(err);
            except.errorType = ET_GIO;
            except.srcUri = m_current_src_uri;
            except.destDirUri = m_current_dest_dir_uri;
            except.op = FileOpCopy;
            except.title = tr("File copy error");
            except.errorCode = err->code;
            if (handle_type == Other) {
                if (G_IO_ERROR_EXISTS == err->code) {
                    except.dlgType = ED_CONFLICT;
                    Q_EMIT errored(except);
                    auto typeData = except.respCode;
                    handle_type = typeData;
                } else {
                    except.dlgType = ED_WARNING;
                    Q_EMIT errored(except);
                    auto typeData = except.respCode;
                    handle_type = typeData;
                }
            }
            //handle.
            switch (handle_type) {
            case IgnoreOne: {
                node->setState(FileNode::Unhandled);
                node->setErrorResponse(IgnoreOne);
                break;
            }
            case IgnoreAll: {
                node->setState(FileNode::Unhandled);
                node->setErrorResponse(IgnoreOne);
                m_prehandle_hash.insert(err->code, IgnoreOne);
                break;
            }
            case OverWriteOne: {
                node->setState(FileNode::Handled);
                node->setErrorResponse(OverWriteOne);
                //make dir has no overwrite
                break;
            }
            case OverWriteAll: {
                node->setState(FileNode::Handled);
                node->setErrorResponse(OverWriteOne);
                m_prehandle_hash.insert(err->code, OverWriteOne);
                break;
            }
            case BackupOne: {
                node->setState(FileNode::Handled);
                node->setErrorResponse(BackupOne);
                // use custom name
                QString name = "";
                QStringList extendStr = node->destBaseName().split(".");
                if (extendStr.length() > 0) {
                    extendStr.removeAt(0);
                }
                QString endStr = extendStr.join(".");
                if (except.respValue.contains("name")) {
                    name = except.respValue["name"].toString();
                    if (endStr != "" && name.endsWith(endStr)) {
                        node->setDestFileName(name);
                    } else if ("" != endStr && "" != name) {
                        node->setDestFileName(name + "." + endStr);
                    }
                }
                while (FileUtils::isFileExsit(node->resolveDestFileUri(m_dest_dir_uri))) {
                    handleDuplicate(node);
                }
                goto fallback_retry;
            }
            case BackupAll: {
                node->setState(FileNode::Handled);
                node->setErrorResponse(BackupOne);
                while (FileUtils::isFileExsit(node->resolveDestFileUri(m_dest_dir_uri))) {
                    handleDuplicate(node);
                }
                //make dir has no backup
                m_prehandle_hash.insert(err->code, BackupOne);
                goto fallback_retry;
            }
            case Retry: {
                goto fallback_retry;
            }
            case Cancel: {
                node->setState(FileNode::Unhandled);
                cancel();
                break;
            }
            default:
                break;
            }
        } else {
            node->setState(FileNode::Handled);
        }
        //assume that make dir finished anyway
        m_current_offset += node->size();
        Q_EMIT operationProgressedOne(node->uri(), node->destUri(), node->size());
        for (auto child : *(node->children())) {
            copyRecursively(child);
        }
    } else {
        GError *err = nullptr;
//        GFileWrapperPtr sourceFile = wrapGFile(g_file_new_for_uri(node->uri().toUtf8().constData()));
//        g_file_copy(sourceFile.get()->get(),
//                    destFile.get()->get(),
//                    m_default_copy_flag,
//                    getCancellable().get()->get(),
//                    GFileProgressCallback(progress_callback),
//                    this,
//                    &err);


        FileCopy fileCopy (node->uri(), destFileUri, m_default_copy_flag,
                           getCancellable().get()->get(),
                           GFileProgressCallback(progress_callback),
                           this,
                           &err);
        if (m_is_pause) fileCopy.pause();
        fileCopy.connect(this, &FileOperation::operationPause, &fileCopy, &FileCopy::pause, Qt::DirectConnection);
        fileCopy.connect(this, &FileOperation::operationResume, &fileCopy, &FileCopy::resume, Qt::DirectConnection);
        fileCopy.connect(this, &FileOperation::operationCancel, &fileCopy, &FileCopy::cancel, Qt::DirectConnection);
        if (m_is_pause) fileCopy.pause();
        fileCopy.run();
        if (err) {
            switch (err->code) {
            case G_IO_ERROR_INVALID_FILENAME: {
                QString newDestUri;
                if (makeFileNameValidForDestFS(m_current_src_uri, m_dest_dir_uri, &newDestUri)) {
                    if (newDestUri != destName) {
                        destName = newDestUri;
                        node->setDestFileName(newDestUri);
                        goto fallback_retry;
                    }
                }
                break;
            }
            case G_IO_ERROR_CANCELLED:
                return;
            case G_IO_ERROR_EXISTS:
                char* destFileName = g_file_get_uri(destFile.get()->get());
                if (NULL != destFileName) {
                    m_conflict_files << destFileName;
                    g_free(destFileName);
                }
                break;
            }

            FileOperationError except;
            auto errWrapperPtr = GErrorWrapper::wrapFrom(err);
            QString str_error = QObject::tr("Error when getting information for file : No target file found");
            int handle_type = prehandle(err);
            except.errorType = ET_GIO;
            except.op = FileOpCopy;
            except.title = tr("File copy error");
            except.srcUri = m_current_src_uri;
            except.errorCode = err->code;
            except.errorStr = str_error;
            except.destDirUri = m_current_dest_dir_uri;

            //
            if (err->code == G_IO_ERROR_PERMISSION_DENIED) {
                except.errorStr = tr("Cannot opening file, permission denied!");
            }

            if (handle_type == Other) {
                if (G_IO_ERROR_EXISTS == err->code) {
                    except.dlgType = ED_CONFLICT;
                    Q_EMIT errored(except);
                    auto typeData = except.respCode;
                    qDebug()<<"get return";
                    handle_type = typeData;
                } else {
                    except.dlgType = ED_WARNING;
                    Q_EMIT errored(except);
                    auto typeData = except.respCode;
                    qDebug()<<"get return";
                    handle_type = typeData;
                }
            }
            //handle.
            switch (handle_type) {
            case IgnoreOne: {
                node->setState(FileNode::Unhandled);
                node->setErrorResponse(IgnoreOne);
                break;
            }
            case IgnoreAll: {
                node->setState(FileNode::Unhandled);
                node->setErrorResponse(IgnoreOne);
                m_prehandle_hash.insert(err->code, IgnoreOne);
                break;
            }
            case OverWriteOne: {
//                FileCopy fileCopy(sourceFile.get()->get(),
//                            destFile.get()->get(),
//                            GFileCopyFlags(m_default_copy_flag | G_FILE_COPY_OVERWRITE),
//                            getCancellable().get()->get(),
//                            GFileProgressCallback(progress_callback),
//                            this,
//                            nullptr);
                FileCopy fileOverWriteOneCopy (node->uri(), destFileUri,
                                   (GFileCopyFlags)(m_default_copy_flag | G_FILE_COPY_OVERWRITE),
                                   getCancellable().get()->get(),
                                   GFileProgressCallback(progress_callback),
                                   this,
                                   nullptr);
                fileCopy.connect(this, &FileOperation::operationPause, &fileCopy, &FileCopy::pause, Qt::DirectConnection);
                fileCopy.connect(this, &FileOperation::operationResume, &fileCopy, &FileCopy::resume, Qt::DirectConnection);
                fileCopy.connect(this, &FileOperation::operationCancel, &fileCopy, &FileCopy::cancel, Qt::DirectConnection);
                if (m_is_pause) fileCopy.pause();
                fileCopy.run();
                node->setState(FileNode::Handled);
                node->setErrorResponse(OverWriteOne);
                break;
            }
            case OverWriteAll: {
//                g_file_copy(sourceFile.get()->get(),
//                            destFile.get()->get(),
//                            GFileCopyFlags(m_default_copy_flag | G_FILE_COPY_OVERWRITE),
//                            getCancellable().get()->get(),
//                            GFileProgressCallback(progress_callback),
//                            this,
//                            nullptr);
                FileCopy fileOverWriteOneCopy (node->uri(), destFileUri,
                                   (GFileCopyFlags)(m_default_copy_flag | G_FILE_COPY_OVERWRITE),
                                   getCancellable().get()->get(),
                                   GFileProgressCallback(progress_callback),
                                   this,
                                   nullptr);
                fileCopy.connect(this, &FileOperation::operationPause, &fileCopy, &FileCopy::pause, Qt::DirectConnection);
                fileCopy.connect(this, &FileOperation::operationResume, &fileCopy, &FileCopy::resume, Qt::DirectConnection);
                fileCopy.connect(this, &FileOperation::operationCancel, &fileCopy, &FileCopy::cancel, Qt::DirectConnection);
                if (m_is_pause) fileCopy.pause();
                fileCopy.run();
                node->setState(FileNode::Handled);
                node->setErrorResponse(OverWriteOne);
                m_prehandle_hash.insert(err->code, OverWriteOne);
                break;
            }
            case BackupOne: {
                node->setState(FileNode::Handled);
                node->setErrorResponse(BackupOne);
                // use custom name
                QString name = "";
                QStringList extendStr = node->destBaseName().split(".");
                if (extendStr.length() > 0) {
                    extendStr.removeAt(0);
                }
                QString endStr = extendStr.join(".");
                if (except.respValue.contains("name")) {
                    name = except.respValue["name"].toString();
                    if (endStr != "" && name.endsWith(endStr)) {
                        node->setDestFileName(name);
                    } else if ("" != endStr && "" != name) {
                        node->setDestFileName(name + "." + endStr);
                    }
                }

                while (FileUtils::isFileExsit(node->resolveDestFileUri(m_dest_dir_uri))) {
                    handleDuplicate(node);
                }
                goto fallback_retry;
            }
            case BackupAll: {
                node->setState(FileNode::Handled);
                node->setErrorResponse(BackupOne);
                while (FileUtils::isFileExsit(node->resolveDestFileUri(m_dest_dir_uri))) {
                    handleDuplicate(node);
                }
                m_prehandle_hash.insert(err->code, BackupOne);
                goto fallback_retry;
            }
            case Retry: {
                goto fallback_retry;
            }
            case Cancel: {
                node->setState(FileNode::Unhandled);
                cancel();
                break;
            }
            default:
                break;
            }
        } else {
            node->setState(FileNode::Handled);
        }
        m_current_offset += node->size();
        fileSync(srcUri, destFileUri);
        Q_EMIT operationProgressedOne(node->uri(), node->destUri(), node->size());
    }
    destFile.reset();
}

void FileCopyOperation::rollbackNodeRecursively(FileNode *node)
{
    switch (node->state()) {
    case FileNode::Handling:
    case FileNode::Handled: {
        if (node->isFolder()) {
            auto children = node->children();
            for (auto child : *children) {
                rollbackNodeRecursively(child);
            }
            GFile *dest_file = g_file_new_for_uri(node->destUri().toUtf8().constData());
            //FIXME: there's a certain probability of failure to delete the folder without
            //any problem happended. because somehow an empty file will created in the folder.
            //i don't know why, but it is obvious that i have to delete them at first.
            bool is_folder_deleted = g_file_delete(dest_file, nullptr, nullptr);
            if (!is_folder_deleted) {
                FileEnumerator e;
                e.setEnumerateDirectory(node->destUri());
                e.enumerateSync();
                for (auto folder_child : *node->children()) {
                    if (!folder_child->destUri().isEmpty()) {
                        GFile *tmp_file = g_file_new_for_uri(folder_child->destUri().toUtf8().constData());
                        g_file_delete(tmp_file, nullptr, nullptr);
                        g_object_unref(tmp_file);
                    }
                    g_file_delete(dest_file, nullptr, nullptr);
                }
            }
            g_object_unref(dest_file);
        } else {
            if (!m_conflict_files.contains(node->destUri())) {
                GFile *dest_file = g_file_new_for_uri(node->destUri().toUtf8().constData());
                g_file_delete(dest_file, nullptr, nullptr);
                g_object_unref(dest_file);
            }
        }
        operationRollbackedOne(node->destUri(), node->uri());
        break;
    }
    default: {
        //make sure all nodes were rollbacked.
        if (node->isFolder()) {
            auto children = node->children();
            for (auto child : *children) {
                rollbackNodeRecursively(child);
            }
        }
        break;
    }
    }
}

void FileCopyOperation::run()
{
    if (isCancelled())
        return;

    Q_EMIT operationStarted();

    Q_EMIT operationRequestShowWizard();

    goffset *total_size = new goffset(0);

    QList<FileNode*> nodes;
    for (auto uri : m_source_uris) {
        qDebug() << "copy uri:" << uri;
        FileNode *node = new FileNode(FileUtils::urlDecode(uri), nullptr, m_reporter);
        node->findChildrenRecursively();
        node->computeTotalSize(total_size);
        nodes << node;
    }

    Q_EMIT operationPrepared();

    m_total_szie = *total_size;
    delete total_size;

    for (auto node : nodes) {
        copyRecursively(node);
    }
    Q_EMIT operationProgressed();

    if (isCancelled()) {
        Q_EMIT operationStartRollbacked();
        for (auto file : nodes) {
            qDebug()<<file->uri();
            if (isCancelled()) {
                rollbackNodeRecursively(file);
            }
        }
    }

    setHasError(false);

    for (auto node : nodes) {
        if (!isCancelled())
            m_info->m_node_map.insert(node->uri(), node->destUri());
        delete node;
    }

    m_info->m_dest_uris = m_info->m_node_map.values();

    nodes.clear();

    Q_EMIT operationFinished();
}

void FileCopyOperation::cancel()
{
    if (m_reporter) {
        m_reporter->cancel();
    }

    ClipboardUtils::popLastTargetDirectoryUri(m_dest_dir_uri);

    FileOperation::cancel();
}

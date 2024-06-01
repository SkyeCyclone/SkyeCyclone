const { ipc } = process.electronBinding('ipc');
const v8Util = process.electronBinding('v8_util');

// Created by init.js.
export const ipcRendererInternal = v8Util.getHiddenValue<Electron.IpcRendererInternal>(global, 'ipc-internal');
const internal = true;

// TODO(MarshallOfSound): Remove if statement when isolated_bundle and content_script_bundle are gone
if (!ipcRendererInternal.send) {
  ipcRendererInternal.send = function (channel, ...args) {
    return ipc.send(internal, channel, args);
  };

  ipcRendererInternal.sendSync = function (channel, ...args) {
    return ipc.sendSync(internal, channel, args)[0];
  };

  ipcRendererInternal.sendTo = function (webContentsId, channel, ...args) {
    return ipc.sendTo(internal, false, webContentsId, channel, args);
  };

  ipcRendererInternal.sendToAll = function (webContentsId, channel, ...args) {
    return ipc.sendTo(internal, true, webContentsId, channel, args);
  };

  ipcRendererInternal.invoke = async function<T> (channel: string, ...args: any[]) {
    const { error, result } = await ipc.invoke<T>(internal, channel, args);
    if (error) {
      throw new Error(`Error invoking remote method '${channel}': ${error}`);
    }
    return result;
  };

  ipcRendererInternal.onMessageFromMain = function (channel: string, listener: (event: Electron.IpcRendererEvent, ...args: any[]) => void) {
    return ipcRendererInternal.on(channel, (event, ...args) => {
      if (event.senderId !== 0) {
        console.error(`Message ${channel} sent by unexpected WebContents (${event.senderId})`);
        return;
      }

      listener(event, ...args);
    });
  };

  ipcRendererInternal.onceMessageFromMain = function (channel: string, listener: (event: Electron.IpcRendererEvent, ...args: any[]) => void) {
    return ipcRendererInternal.on(channel, function wrapper (event, ...args) {
      if (event.senderId !== 0) {
        console.error(`Message ${channel} sent by unexpected WebContents (${event.senderId})`);
        return;
      }

      ipcRendererInternal.removeListener(channel, wrapper);
      listener(event, ...args);
    });
  };
}

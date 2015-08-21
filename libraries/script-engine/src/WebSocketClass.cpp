//
//  WebSocketClass.cpp
//  libraries/script-engine/src/
//
//  Created by Thijs Wenker on 8/4/15.
//  Copyright (c) 2015 High Fidelity, Inc. All rights reserved.
//
//  This class is an implementation of the WebSocket object for scripting use.  It provides a near-complete implementation
//  of the class described in the Mozilla docs: https://developer.mozilla.org/en-US/docs/Web/API/WebSocket
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ScriptEngine.h"
#include "WebSocketClass.h"

WebSocketClass::WebSocketClass(QScriptEngine* engine, QString url) :
    _engine(engine),
    _webSocket(new QWebSocket())
{
    initialize();
    _webSocket->open(url);
}

WebSocketClass::WebSocketClass(QScriptEngine* engine, QWebSocket* qWebSocket) :
    _engine(engine),
    _webSocket(qWebSocket)
{
    initialize();
}

void WebSocketClass::initialize() {
    connect(_webSocket, &QWebSocket::disconnected, this, &WebSocketClass::handleOnClose);
    connect(_webSocket, &QWebSocket::textMessageReceived, this, &WebSocketClass::handleOnMessage);
    connect(_webSocket, &QWebSocket::connected, this, &WebSocketClass::handleOnOpen);
    _binaryType = QStringLiteral("blob");
}

QScriptValue WebSocketClass::constructor(QScriptContext* context, QScriptEngine* engine) {
    QString url;
    if (context->argumentCount() > 0) {
        url = context->argument(0).toString();
    }
    return engine->newQObject(new WebSocketClass(engine, url));
}

WebSocketClass::~WebSocketClass() {
    _webSocket->deleteLater();
}

void WebSocketClass::send(QScriptValue message) {
    _webSocket->sendTextMessage(message.toString());
}

void WebSocketClass::close() {
    this->close(QWebSocketProtocol::CloseCode::CloseCodeNormal);
}

void WebSocketClass::close(QWebSocketProtocol::CloseCode closeCode) {
    this->close(closeCode, QStringLiteral(""));
}

void WebSocketClass::close(QWebSocketProtocol::CloseCode closeCode, QString reason) {
    _webSocket->close(closeCode, reason);
}

void WebSocketClass::handleOnClose() {
    bool hasError = false;
    if (_webSocket->error() != QAbstractSocket::SocketError::UnknownSocketError) {
        hasError = true;
        if (_onErrorEvent.isFunction()) {
            _onErrorEvent.call();
        }
    }
    if (_onCloseEvent.isFunction()) {
        QScriptValueList args;
        QScriptValue arg = _engine->newObject();
        arg.setProperty("code", hasError ? QWebSocketProtocol::CloseCode::CloseCodeAbnormalDisconnection : _webSocket->closeCode());
        arg.setProperty("reason", _webSocket->closeReason());
        arg.setProperty("wasClean", !hasError);
        args << arg;
        _onCloseEvent.call(QScriptValue(), args);
    }
}

void WebSocketClass::handleOnMessage(const QString& message) {
    if (_onMessageEvent.isFunction()) {
        QScriptValueList args;
        QScriptValue arg = _engine->newObject();
        arg.setProperty("data", message);
        args << arg;
        _onMessageEvent.call(QScriptValue(), args);
    }
}

void WebSocketClass::handleOnOpen() {
    if (_onOpenEvent.isFunction()) {
        _onOpenEvent.call();
    }
}

QScriptValue qWSCloseCodeToScriptValue(QScriptEngine* engine, const QWebSocketProtocol::CloseCode &closeCode) {
    return closeCode;
}

void qWSCloseCodeFromScriptValue(const QScriptValue &object, QWebSocketProtocol::CloseCode &closeCode) {
    closeCode = (QWebSocketProtocol::CloseCode)object.toUInt16();
}

QScriptValue webSocketToScriptValue(QScriptEngine* engine, WebSocketClass* const &in) {
    return engine->newQObject(in);
}

void webSocketFromScriptValue(const QScriptValue &object, WebSocketClass* &out) {
    out = qobject_cast<WebSocketClass*>(object.toQObject());
}

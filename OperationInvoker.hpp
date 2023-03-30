#pragma once

#include <QtCore/QEvent>
#include <QtCore/QFutureWatcher>
#include <QtCore/QObject>
#include <QtCore/QPointer>

#include "Logger.hpp"

namespace sg::git
{
	class Operation;
	class OperationInvoker: public QObject
	{
		Q_OBJECT
		LOG_CATEGORY("OperationInvoker")

	public:
		static OperationInvoker * instance();

		Operation * execute(Operation * op, bool inSequence = false);

		bool inSequence() const;
		void setInSequence(bool inSequence);

	protected:
		const QEvent::Type PostExecEventType = static_cast<QEvent::Type>(QEvent::User + 1);

		bool event(QEvent * event) override;

		void invokeOperation();

	private:
		explicit OperationInvoker(QObject * parent = nullptr);
		static void runnable(QPointer<Operation> op);

		void prepareAndRun(QPointer<Operation> op);

		void invokeWithPreparation();
		void nextOperation();
		void invokeStep2(bool prepared);
		bool _inPreparation = false;

	private:
		QList<QPointer<Operation>> _parallelOperations;
		QList<QPointer<Operation>> _sequentialOperations;
		QFutureWatcher<void> _sequentialWatcher;

		bool _inSequenceMode = true;

		inline static OperationInvoker * _instance = nullptr;
	};
}  // namespace sg::git

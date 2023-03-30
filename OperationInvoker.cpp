#include "OperationInvoker.hpp"

#include <QtConcurrent/QtConcurrent>
#include <QtCore/QCoreApplication>

#include "Operation.hpp"

using namespace sg::git;

OperationInvoker::OperationInvoker(QObject * parent)
	: QObject{parent}
{
	setInSequence(true);
	// connect(&_sequentialWatcher, &QFutureWatcherBase::finished, this, &OperationInvoker::invokeOperation);

	connect(
		&_sequentialWatcher,
		&QFutureWatcherBase::finished,
		this,
		[this]()
		{
			qCritical() << "$$$$$$$$$$$ INVIKER:: NEXT OPERATION";
			invokeOperation();
			// invokeWithPreparation();
		});
}

Operation * OperationInvoker::execute(Operation * op, bool inSequence)
{
	if (op)
	{
		logger()->info("Prepare to exectution of operation '{}'", op->name());

		if (_inSequenceMode || inSequence)
		{
			if (!_sequentialOperations.isEmpty())
			{
				logger()->info("Operation '{}' is queued", op->name());
			}
			_sequentialOperations << op;
		}
		else
		{
			_parallelOperations << op;
		}
		// return operation to interact with it before operation will be run
		// Low priority: handle all events before running
		qApp->postEvent(this, new QEvent(PostExecEventType), Qt::LowEventPriority);
	}
	return op;
}

bool OperationInvoker::event(QEvent * event)
{
	if (event->type() == PostExecEventType)
	{
		invokeOperation();
		// invokeWithPreparation();
		event->accept();
		return true;
	}
	return QObject::event(event);
}

void OperationInvoker::runnable(QPointer<Operation> op)
{
	QEventLoop wait;
	QTimer terminator;
	bool prepared = false;

	auto onReady = [&prepared, &wait](bool ready)
	{
		logger()->info("Preparations are done");
		prepared = ready;
		wait.quit();
	};

	auto onTerminated = [op]()
	{
		logger()->warn("Operation: '{}' is Terminated. The waiting time has expired", op->name());
		op->ready(false);
	};

	connect(op, &Operation::ready, &wait, onReady, Qt::QueuedConnection);
	connect(op, &Operation::destroyed, &wait, &QEventLoop::quit);
	terminator.singleShot(5000, &wait, onTerminated);  // 20000

	qCritical() << "before QEventLoo pexec" << op->name();
	op->prepare();
	wait.exec();

	if (op.isNull())
		return;
	qCritical() << "after QEventLoop exec" << op->name();

	op->disconnect(&wait);
	if (prepared)
	{
		qCritical() << "STEP 2" << op->name();
		op->run();
	}
}

void OperationInvoker::prepareAndRun(QPointer<Operation> op)
{
	auto f = QtFuture::connect(op.data(), &Operation::ready);
	std::function<void(bool)> func = [op](bool res)
	{
		qCritical() << "STEP 2" << op->name() << res;
		op->run();
	};

	auto t = f.then(QtFuture::Launch::Async, func);
	op->prepare();
	_sequentialWatcher.setFuture(t);
}

void OperationInvoker::invokeWithPreparation()
{
	if (!_sequentialOperations.isEmpty() && !_sequentialWatcher.isRunning())
	{
		QPointer<Operation> next;
		while (!next && !_sequentialOperations.isEmpty())
		{
			next = _sequentialOperations.takeFirst();
		}
		if (next)
		{
			connect(next, &Operation::ready, this, &OperationInvoker::invokeStep2);
			connect(next, &Operation::destroyed, this, &OperationInvoker::nextOperation);
			//			QTimer::singleShot(
			//				5000,
			//				next,
			//				[next]()
			//				{
			//					next->ready(false);
			//				});

			_inPreparation = true;
			next->prepare();
		}
	}
}

void OperationInvoker::nextOperation()
{
	_inPreparation = false;
	invokeWithPreparation();
}

void OperationInvoker::invokeStep2(bool prepared)
{
	auto op = qobject_cast<Operation *>(sender());
	_inPreparation = false;
	disconnect(op, &Operation::destroyed, this, &OperationInvoker::nextOperation);
	disconnect(op, &Operation::ready, this, &OperationInvoker::invokeStep2);
	if (prepared)
	{
		qCritical() << "$$$$$$$$$$$ INVIKER::STEP 2";
		_sequentialWatcher.setFuture(QtConcurrent::run(&Operation::run, op));
	}
	else
	{
		qCritical() << "$$$$$$$$$$$ INVIKER:: CANCELED NEXT OPERATION";
		nextOperation();
	}
}

void OperationInvoker::invokeOperation()
{
	static auto run = [this](QList<QPointer<Operation>> & operations) -> QFuture<void>
	{
		QPointer<Operation> op;
		while (!operations.isEmpty())
		{
			op = operations.takeFirst();

			if (!op.isNull())
			{
				logger()->info("Run operation: '{}'. Operations in queue: {}", op->name(), operations.count());
				qCritical() << "Next operation in thread" << op->name() << QThread::currentThread();

				return QtConcurrent::run(&OperationInvoker::runnable, op);
			}
			else
			{
				logger()->warn("Broken Operation pointer.");
			}
		}

		return {};
	};
	qCritical() << "FUTURE WATCHER: isRunning" << _sequentialWatcher.isRunning() << "IS started"
				<< _sequentialWatcher.isStarted();
	if (!_sequentialOperations.isEmpty() && !_sequentialWatcher.isRunning() && !_inPreparation)
	{
		_sequentialWatcher.setFuture(run(_sequentialOperations));
	}
	else if (!_parallelOperations.isEmpty())
	{
		run(_parallelOperations);
	}
}

bool OperationInvoker::inSequence() const
{
	return _inSequenceMode;
}

void OperationInvoker::setInSequence(bool inSequence)
{
	if (_inSequenceMode != inSequence)
	{
		_inSequenceMode = inSequence;
	}
}

OperationInvoker * OperationInvoker::instance()
{
	if (!_instance)
	{
		_instance = new OperationInvoker;
	}

	return _instance;
}


//! Quantum-Execution-Engine — Order Router (Rust)
//!
//! Asynchronous, lock-free oriented routing layer.
//! Uses Tokio for task scheduling and crossbeam-style channels
//! for high-throughput signal delivery under tight latency budgets.
//!
//! Design goals:
//!   - Ownership safety
//!   - Minimal allocations on the hot path
//!   - Explicit error surfaces
//!   - Sub-50 µs routing decision target

use std::sync::Arc;
use tokio::sync::mpsc;
use thiserror::Error;

/// Maximum time (in microseconds) we allow for a routing decision.
const LATENCY_BUDGET_US: u64 = 50;

#[derive(Debug, Clone)]
pub struct OrderSignal {
    pub symbol: String,
    pub side: Side,
    pub quantity: f64,
    pub limit_price: Option<f64>,
    pub client_tag: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Side {
    Buy,
    Sell,
}

#[derive(Debug, Error)]
pub enum RouterError {
    #[error("channel closed")]
    ChannelClosed,
    #[error("latency budget exceeded")]
    LatencyBudgetExceeded,
    #[error("invalid quantity: {0}")]
    InvalidQuantity(f64),
    #[error("risk rejection")]
    RiskRejected,
}

/// Lock-free-ish ring buffer concept (simplified for showcase).
/// In production this would be a proper SPSC/MPSC ring with cache-line padding.
pub struct OrderRouter {
    tx: mpsc::Sender<OrderSignal>,
    /// Shared counter for observability (atomic in real code)
    routed_count: Arc<std::sync::atomic::AtomicU64>,
}

impl OrderRouter {
    /// Create a router with a bounded channel (back-pressure).
    pub fn new(buffer_size: usize) -> (Self, mpsc::Receiver<OrderSignal>) {
        let (tx, rx) = mpsc::channel(buffer_size);
        let router = Self {
            tx,
            routed_count: Arc::new(std::sync::atomic::AtomicU64::new(0)),
        };
        (router, rx)
    }

    /// Attempt to route an order. Fails closed on any validation error.
    pub async fn route(&self, signal: OrderSignal) -> Result<(), RouterError> {
        if signal.quantity <= 0.0 {
            return Err(RouterError::InvalidQuantity(signal.quantity));
        }

        // In a real system we would measure TSC / Instant here
        // and enforce LATENCY_BUDGET_US.

        self.tx
            .send(signal)
            .await
            .map_err(|_| RouterError::ChannelClosed)?;

        self.routed_count
            .fetch_add(1, std::sync::atomic::Ordering::Relaxed);

        Ok(())
    }

    /// Zero-copy style hand-off illustration.
    /// The signal is moved, not cloned, into the channel.
    pub async fn route_many(&self, signals: Vec<OrderSignal>) -> Result<usize, RouterError> {
        let mut accepted = 0usize;
        for signal in signals {
            match self.route(signal).await {
                Ok(()) => accepted += 1,
                Err(RouterError::ChannelClosed) => return Err(RouterError::ChannelClosed),
                Err(_) => continue, // drop individual bad signals
            }
        }
        Ok(accepted)
    }

    pub fn routed_count(&self) -> u64 {
        self.routed_count.load(std::sync::atomic::Ordering::Relaxed)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn route_accepts_valid_order() {
        let (router, mut rx) = OrderRouter::new(16);
        let signal = OrderSignal {
            symbol: "EURUSD".into(),
            side: Side::Buy,
            quantity: 1.0,
            limit_price: Some(1.0850),
            client_tag: 42,
        };

        router.route(signal.clone()).await.expect("route failed");
        let received = rx.recv().await.expect("no message");
        assert_eq!(received.client_tag, 42);
        assert_eq!(router.routed_count(), 1);
    }

    #[tokio::test]
    async fn route_rejects_zero_quantity() {
        let (router, _rx) = OrderRouter::new(8);
        let bad = OrderSignal {
            symbol: "EURUSD".into(),
            side: Side::Sell,
            quantity: 0.0,
            limit_price: None,
            client_tag: 1,
        };
        let err = router.route(bad).await.unwrap_err();
        assert!(matches!(err, RouterError::InvalidQuantity(_)));
    }
}

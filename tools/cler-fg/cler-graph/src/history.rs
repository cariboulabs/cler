use std::collections::VecDeque;

pub const ACTION_HISTORY_CAPACITY: usize = 256;

pub struct ActionQueue<T, const CAPACITY: usize = ACTION_HISTORY_CAPACITY> {
    entries: VecDeque<T>,
    cursor: usize,
}

impl<T, const CAPACITY: usize> Default for ActionQueue<T, CAPACITY> {
    fn default() -> Self {
        assert!(CAPACITY > 0);
        Self {
            entries: VecDeque::with_capacity(CAPACITY),
            cursor: 0,
        }
    }
}

impl<T, const CAPACITY: usize> ActionQueue<T, CAPACITY> {
    pub fn push(&mut self, action: T) {
        self.entries.truncate(self.cursor);
        if self.entries.len() == CAPACITY {
            self.entries.pop_front();
            self.cursor -= 1;
        }
        self.entries.push_back(action);
        self.cursor += 1;
    }

    pub fn undo(&self) -> Option<&T> {
        self.cursor
            .checked_sub(1)
            .and_then(|index| self.entries.get(index))
    }

    pub fn redo(&self) -> Option<&T> {
        self.entries.get(self.cursor)
    }

    pub fn commit_undo(&mut self) {
        assert!(self.can_undo());
        self.cursor -= 1;
    }

    pub fn commit_redo(&mut self) {
        assert!(self.can_redo());
        self.cursor += 1;
    }

    pub fn can_undo(&self) -> bool {
        self.cursor > 0
    }

    pub fn can_redo(&self) -> bool {
        self.cursor < self.entries.len()
    }

    pub fn clear(&mut self) {
        self.entries.clear();
        self.cursor = 0;
    }

    #[cfg(test)]
    pub fn len(&self) -> usize {
        self.entries.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn queue_is_bounded_and_discards_redo_on_a_new_action() {
        let mut queue = ActionQueue::<usize, 3>::default();
        for action in 0..5 {
            queue.push(action);
        }
        assert_eq!(queue.len(), 3);
        assert_eq!(queue.undo(), Some(&4));
        queue.commit_undo();
        assert_eq!(queue.undo(), Some(&3));
        assert_eq!(queue.redo(), Some(&4));
        queue.push(9);
        assert_eq!(queue.redo(), None);
        assert_eq!(queue.undo(), Some(&9));
    }
}

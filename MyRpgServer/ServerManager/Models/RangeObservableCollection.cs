using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;

namespace RpgServerManager.Models;
public sealed class RangeObservableCollection<T> : ObservableCollection<T>
{
    public void TrimStart(int count)
    {
        if (count <= 0)
        {
            return;
        }

        CheckReentrancy();
        count = Math.Min(count, Items.Count);
        if (Items is List<T> list)
        {
            list.RemoveRange(0, count);
        }
        else
        {
            for (var index = 0; index < count; ++index)
            {
                Items.RemoveAt(0);
            }

        }

        OnPropertyChanged(new PropertyChangedEventArgs(nameof(Count)));
        OnPropertyChanged(new PropertyChangedEventArgs("Item[]"));
        OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
    }

}


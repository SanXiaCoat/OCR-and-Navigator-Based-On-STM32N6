package com.sanxia.stm32ARmap;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.amap.api.services.core.PoiItem;

import java.util.List;

public class SearchResultAdapter extends RecyclerView.Adapter<SearchResultAdapter.VH> {

    private final List<PoiItem> poiList;
    private final OnItemClickListener clickListener;

    public interface OnItemClickListener {
        void onItemClick(PoiItem item);
    }

    public SearchResultAdapter(List<PoiItem> poiList, OnItemClickListener listener) {
        this.poiList = poiList;
        this.clickListener = listener;
    }

    @NonNull
    @Override
    public VH onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View view = LayoutInflater.from(parent.getContext())
                .inflate(R.layout.item_search_poi, parent, false);
        return new VH(view);
    }

    @Override
    public void onBindViewHolder(@NonNull VH holder, int position) {
        PoiItem item = poiList.get(position);
        holder.tvTitle.setText(item.getTitle());
        holder.tvAddress.setText(item.getSnippet());
        holder.itemView.setOnClickListener(v -> {
            if (clickListener != null) {
                clickListener.onItemClick(item);
            }
        });
    }

    @Override
    public int getItemCount() {
        return poiList == null ? 0 : poiList.size();
    }

    public static class VH extends RecyclerView.ViewHolder {
        TextView tvTitle;
        TextView tvAddress;

        public VH(@NonNull View itemView) {
            super(itemView);
            tvTitle = itemView.findViewById(R.id.tv_title);
            tvAddress = itemView.findViewById(R.id.tv_address);
        }
    }
}
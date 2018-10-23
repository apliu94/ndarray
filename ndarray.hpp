#include <memory>
#include <vector>
#include <array>
#include <numeric>




// ============================================================================
template<int Rank, int Axis=0>
struct selector
{


    enum { rank = Rank, axis = Axis };


    /**
     * Collapse this selector at the given index, creating a selector with
     * rank reduced by 1, and which operates on the same axis.
     */
    selector<rank - 1, axis> collapse(int start_index) const
    {
        static_assert(rank > 0, "selector: cannot collapse zero-rank selector");
        static_assert(axis < rank, "selector: attempting to index on axis >= rank");

        std::array<int, rank - 1> _count;
        std::array<int, rank - 1> _start;
        std::array<int, rank - 1> _final;

        for (int n = 0; n < axis; ++n)
        {
            _count[n] = count[n];
            _start[n] = start[n];
            _final[n] = final[n];
        }

        for (int n = axis + 1; n < rank - 1; ++n)
        {
            _count[n] = count[n + 1];
            _start[n] = start[n + 1];
            _final[n] = final[n + 1];
        }

        _count[axis] = count[axis] * count[axis + 1];
        _start[axis] = count[axis] * start[axis] + start[axis + 1] + start_index;
        _final[axis] = count[axis] * start[axis] + final[axis + 1];

        return {_count, _start, _final};
    }

    selector<rank - 1, axis> within(int start_index) const
    {
        return collapse(start_index);
    }

    selector<rank, axis + 1> within(std::tuple<int, int> range) const
    {
        static_assert(axis < rank, "selector: attempting to index on axis >= rank");

        auto _count = count;
        auto _start = start;
        auto _final = final;

        _start[axis] = start[axis] + std::get<0>(range);
        _final[axis] = start[axis] + std::get<1>(range);

        return {_count, _start, _final};
    }

    template<typename First, typename... Rest>
    auto within(First first, Rest... rest) const
    {
        return within(first).within(rest...);
    }

    selector<rank> reset() const
    {
        return selector<rank>{count, start, final};
    }

    std::array<int, rank> shape() const
    {
        std::array<int, rank> s;

        for (int n = 0; n < rank; ++n)
        {
            s[n] = final[n] - start[n];
        }
        return s;
    }

    int size() const
    {
        auto s = shape();
        return std::accumulate(s.begin(), s.end(), 1, std::multiplies<>());
    }

    bool operator==(const selector<rank, axis>& other) const
    {
        return count == other.count && start == other.start && final == other.final;
    }

    bool operator!=(const selector<rank, axis>& other) const
    {
        return ! operator==(other);
    }




    /** Data members
     *
     */
    // ========================================================================
    std::array<int, rank> count;
    std::array<int, rank> start;
    std::array<int, rank> final;
};




// ============================================================================
template<int Rank>
class ndarray
{
public:


    enum { rank = Rank };


    /**
     * Constructors
     * 
     */
    // ========================================================================
    template<int R = rank, typename = typename std::enable_if<R == 0>::type>
    ndarray(double value=double())
    : scalar_offset(0)
    , data(std::make_shared<std::vector<double>>(1, value))
    {
    }

    template<int R = rank, typename = typename std::enable_if<R == 0>::type>
    ndarray(int scalar_offset, std::shared_ptr<std::vector<double>> data)
    : scalar_offset(scalar_offset)
    , data(data)
    {
    }

    template<int R = rank, typename = typename std::enable_if<R == 1>::type>
    ndarray(std::initializer_list<double> elements)
    : count({int(elements.size())})
    , start({0})
    , final({int(elements.size())})
    , skips({1})
    , strides({1})
    , data(std::make_shared<std::vector<double>>(elements.begin(), elements.end()))
    {
    }

    template<typename... Dims>
    ndarray(Dims... dims) : ndarray(std::array<int, rank>({dims...}))
    {
        static_assert(sizeof...(dims) == rank,
          "Number of arguments to ndarray constructor must match rank");
    }

    template<typename SelectorType>
    ndarray(SelectorType selector, std::shared_ptr<std::vector<double>> data)
    : count(selector.count)
    , start(selector.start)
    , final(selector.final)
    , skips(constant_array<int, rank>(1))
    , strides(compute_strides(count))
    , data(data)
    {
    }

    ndarray(std::array<int, rank> dim_sizes)
    : count(dim_sizes)
    , start(constant_array<int, rank>(0))
    , final(dim_sizes)
    , skips(constant_array<int, rank>(1))
    , strides(compute_strides(count))
    , data(std::make_shared<std::vector<double>>(product(dim_sizes)))
    {
    }

    ndarray(std::array<int, rank> dim_sizes, std::shared_ptr<std::vector<double>> data)
    : count(dim_sizes)
    , start(constant_array<int, rank>(0))
    , final(dim_sizes)
    , skips(constant_array<int, rank>(1))
    , strides(compute_strides(count))
    , data(data)
    {
        assert(data->size() == std::accumulate(count.begin(), count.end(), 1, std::multiplies<>()));
    }

    ndarray(
        std::array<int, rank> count,
        std::array<int, rank> start,
        std::array<int, rank> final,
        std::shared_ptr<std::vector<double>> data)
    : count(count)
    , start(start)
    , final(final)
    , skips(constant_array<int, rank>(1))
    , strides(compute_strides(count))
    , data(data)
    {
        assert(data->size() == std::accumulate(count.begin(), count.end(), 1, std::multiplies<>()));
    }




    /**
     * Shape and size query methods
     * 
     */
    // ========================================================================
    int size() const
    {
        return make_selector().size();
    }
    std::array<int, rank> shape() const
    {
        return make_selector().shape();
    }




    /**
     * Data accessors and selection methods
     * 
     */
    // ========================================================================
    template <int R = rank, typename std::enable_if_t<R == 1>* = nullptr>
    ndarray<rank - 1> operator[](int index)
    {
        return ndarray<0>(offset({index}), data);
    }

    template <int R = rank, typename std::enable_if_t<R != 1>* = nullptr>
    ndarray<rank - 1> operator[](int index)
    {
        return {make_selector().collapse(index), data};
    }

    template<typename... Index>
    double& operator()(Index... index)
    {
        static_assert(sizeof...(index) == rank,
          "Number of arguments to ndarray::operator() must match rank");

        return data->operator[](offset({index...}));
    }

    operator double() const
    {
        static_assert(rank == 0, "can only convert rank-0 array to scalar value");
        return data->operator[](scalar_offset);
    }

    bool contiguous() const
    {
        for (int n = 0; n < rank; ++n)
        {
            if (start[n] != 0 || final[n] != count[n] || skips[n] != 1)
            {
                return false;
            }
        }
        return true;
    }

    template<int other_rank>
    bool shares(const ndarray<other_rank>& other) const
    {
        return data == other.data;
    }




private:
    /**
     * Pivate utility methods
     * 
     */
    // ========================================================================
    int offset(std::array<int, rank> index) const
    {
        int m = scalar_offset;

        for (int n = 0; n < rank; ++n)
        {
            m += start[n] + skips[n] * index[n] * strides[n];
        }
        return m;
    }

    selector<rank> make_selector() const
    {
        return selector<rank>{count, start, final};
    }

    template <int R = rank, typename std::enable_if_t<R == 0>* = nullptr>
    static std::array<int, rank> compute_strides(std::array<int, rank> count)
    {
        return std::array<int, rank>();
    }

    template <int R = rank, typename std::enable_if_t<R != 0>* = nullptr>
    static std::array<int, rank> compute_strides(std::array<int, rank> count)
    {
        std::array<int, rank> s;   
        std::partial_sum(count.rbegin(), count.rend() - 1, s.rbegin() + 1, std::multiplies<>());
        s[rank - 1] = 1;
        return s;
    }

    template<typename T, int length>
    static std::array<T, length> constant_array(T value)
    {
        std::array<T, length> A;
        for (auto& a : A) a = value;
        return A;
    }

    template<typename C>
    static size_t product(const C& c)
    {
        return std::accumulate(c.begin(), c.end(), 1, std::multiplies<>());
    }




    /** Data members
     *
     */
    // ========================================================================
    int scalar_offset = 0;
    std::array<int, rank> count;
    std::array<int, rank> start;
    std::array<int, rank> final;
    std::array<int, rank> skips;
    std::array<int, rank> strides;
    std::shared_ptr<std::vector<double>> data;




    /** Grant friendship to ndarray's of other ranks.
     *
     */
    template<int other_rank>
    friend class ndarray;
};

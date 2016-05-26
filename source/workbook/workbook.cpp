// Copyright (c) 2014-2016 Thomas Fussell
// Copyright (c) 2010-2015 openpyxl
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, WRISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE
//
// @license: http://www.opensource.org/licenses/mit-license.php
// @author: see AUTHORS file
#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <sstream>

#include <xlnt/packaging/app_properties.hpp>
#include <xlnt/packaging/document_properties.hpp>
#include <xlnt/packaging/manifest.hpp>
#include <xlnt/packaging/relationship.hpp>
#include <xlnt/packaging/zip_file.hpp>
#include <xlnt/serialization/encoding.hpp>
#include <xlnt/serialization/excel_serializer.hpp>
#include <xlnt/styles/alignment.hpp>
#include <xlnt/styles/border.hpp>
#include <xlnt/styles/format.hpp>
#include <xlnt/styles/fill.hpp>
#include <xlnt/styles/font.hpp>
#include <xlnt/styles/style.hpp>
#include <xlnt/styles/number_format.hpp>
#include <xlnt/styles/protection.hpp>
#include <xlnt/utils/exceptions.hpp>
#include <xlnt/workbook/const_worksheet_iterator.hpp>
#include <xlnt/workbook/named_range.hpp>
#include <xlnt/workbook/theme.hpp>
#include <xlnt/workbook/workbook.hpp>
#include <xlnt/workbook/worksheet_iterator.hpp>
#include <xlnt/worksheet/range.hpp>
#include <xlnt/worksheet/worksheet.hpp>

#include <detail/cell_impl.hpp>
#include <detail/include_windows.hpp>
#include <detail/workbook_impl.hpp>
#include <detail/worksheet_impl.hpp>

namespace xlnt {
namespace detail {

workbook_impl::workbook_impl()
    : active_sheet_index_(0), guess_types_(false), data_only_(false), read_only_(false), next_custom_format_id_(164)
{
}

} // namespace detail

workbook::workbook() : d_(new detail::workbook_impl())
{
    create_sheet("Sheet");
    
    create_relationship("rId2", "styles.xml", relationship::type::styles);
    create_relationship("rId3", "theme/theme1.xml", relationship::type::theme);
    
    d_->encoding_ = encoding::utf8;
    
    d_->manifest_.add_default_type("rels", "application/vnd.openxmlformats-package.relationships+xml");
    d_->manifest_.add_default_type("xml", "application/xml");
    
    d_->manifest_.add_override_type("/xl/workbook.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");
    d_->manifest_.add_override_type("/xl/theme/theme1.xml", "application/vnd.openxmlformats-officedocument.theme+xml");
    d_->manifest_.add_override_type("/xl/styles.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml");
    d_->manifest_.add_override_type("/docProps/core.xml", "application/vnd.openxmlformats-package.core-properties+xml");
    d_->manifest_.add_override_type("/docProps/app.xml", "application/vnd.openxmlformats-officedocument.extended-properties+xml");
    
    add_format(format());
    create_style("Normal");
}

workbook::workbook(encoding e) : workbook()
{
    d_->encoding_ = e;
}

const worksheet workbook::get_sheet_by_name(const std::string &name) const
{
    for (auto &impl : d_->worksheets_)
    {
        if (impl.title_ == name)
        {
            return worksheet(&impl);
        }
    }

    return worksheet();
}

worksheet workbook::get_sheet_by_name(const std::string &name)
{
	return worksheet(static_cast<const workbook*>(this)->get_sheet_by_name(name));
}

worksheet workbook::get_sheet_by_index(std::size_t index)
{
    return worksheet(&d_->worksheets_[index]);
}

const worksheet workbook::get_sheet_by_index(std::size_t index) const
{
    return worksheet(&d_->worksheets_.at(index));
}

worksheet workbook::get_active_sheet()
{
    return worksheet(&d_->worksheets_[d_->active_sheet_index_]);
}

bool workbook::has_named_range(const std::string &name) const
{
    for (auto worksheet : *this)
    {
        if (worksheet.has_named_range(name))
        {
            return true;
        }
    }
    return false;
}

worksheet workbook::create_sheet()
{
    if(get_read_only()) throw xlnt::read_only_workbook_exception();
    
    std::string title = "Sheet";
    int index = 0;

    while (get_sheet_by_name(title) != nullptr)
    {
        title = "Sheet" + std::to_string(++index);
    }
    
    std::string sheet_filename = "worksheets/sheet" + std::to_string(d_->worksheets_.size() + 1) + ".xml";

    d_->worksheets_.push_back(detail::worksheet_impl(this, title));
    create_relationship("rId" + std::to_string(d_->relationships_.size() + 1),
                        sheet_filename,
                        relationship::type::worksheet);
    
    d_->manifest_.add_override_type("/xl/" + sheet_filename, "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");

    return worksheet(&d_->worksheets_.back());
}

void workbook::add_sheet(xlnt::worksheet worksheet)
{
    if(worksheet.d_->parent_ != this) throw xlnt::value_error();

    xlnt::detail::worksheet_impl impl(*worksheet.d_);
    *create_sheet().d_ = impl;
}

void workbook::add_sheet(xlnt::worksheet worksheet, std::size_t index)
{
    add_sheet(worksheet);
    std::swap(d_->worksheets_[index], d_->worksheets_.back());
}

int workbook::get_index(xlnt::worksheet worksheet)
{
    int i = 0;
    for (auto ws : *this)
    {
        if (worksheet == ws)
        {
            return i;
        }
        i++;
    }
    throw std::runtime_error("worksheet isn't owned by this workbook");
}

void workbook::create_named_range(const std::string &name, worksheet range_owner, const std::string &reference_string)
{
    create_named_range(name, range_owner, range_reference(reference_string));
}

void workbook::create_named_range(const std::string &name, worksheet range_owner, const range_reference &reference)
{
    auto match = get_sheet_by_name(range_owner.get_title());
    if (match != nullptr)
    {
        match.create_named_range(name, reference);
        return;
    }
    throw std::runtime_error("worksheet isn't owned by this workbook");
}

void workbook::remove_named_range(const std::string &name)
{
    for (auto ws : *this)
    {
        if (ws.has_named_range(name))
        {
            ws.remove_named_range(name);
            return;
        }
    }

    throw std::runtime_error("named range not found");
}

range workbook::get_named_range(const std::string &name)
{
    for (auto ws : *this)
    {
        if (ws.has_named_range(name))
        {
            return ws.get_named_range(name);
        }
    }

    throw std::runtime_error("named range not found");
}

bool workbook::load(std::istream &stream)
{
    excel_serializer serializer_(*this);
    serializer_.load_stream_workbook(stream);

    return true;
}

bool workbook::load(const std::vector<unsigned char> &data)
{
    excel_serializer serializer_(*this);
    serializer_.load_virtual_workbook(data);

    return true;
}

bool workbook::load(const std::string &filename)
{
    excel_serializer serializer_(*this);
    serializer_.load_workbook(filename);

    return true;
}

void workbook::set_guess_types(bool guess)
{
    d_->guess_types_ = guess;
}

bool workbook::get_guess_types() const
{
    return d_->guess_types_;
}

void workbook::create_relationship(const std::string &id, const std::string &target, relationship::type type)
{
    d_->relationships_.push_back(relationship(type, id, target));
}

void workbook::create_root_relationship(const std::string &id, const std::string &target, relationship::type type)
{
    d_->root_relationships_.push_back(relationship(type, id, target));
}

relationship workbook::get_relationship(const std::string &id) const
{
    for (auto &rel : d_->relationships_)
    {
        if (rel.get_id() == id)
        {
            return rel;
        }
    }

    throw std::runtime_error("");
}

void workbook::remove_sheet(worksheet ws)
{
    auto match_iter = std::find_if(d_->worksheets_.begin(), d_->worksheets_.end(),
                                   [=](detail::worksheet_impl &comp) { return worksheet(&comp) == ws; });

    if (match_iter == d_->worksheets_.end())
    {
        throw std::runtime_error("worksheet not owned by this workbook");
    }

    auto sheet_filename = "worksheets/sheet" + std::to_string(d_->worksheets_.size()) + ".xml";
    auto rel_iter = std::find_if(d_->relationships_.begin(), d_->relationships_.end(),
                                 [=](relationship &r) { return r.get_target_uri() == sheet_filename; });

    if (rel_iter == d_->relationships_.end())
    {
        throw std::runtime_error("no matching rel found");
    }

    d_->relationships_.erase(rel_iter);
    d_->worksheets_.erase(match_iter);
}

worksheet workbook::create_sheet(std::size_t index)
{
    create_sheet();

    if (index != d_->worksheets_.size() - 1)
    {
        std::swap(d_->worksheets_.back(), d_->worksheets_[index]);
        d_->worksheets_.pop_back();
    }

    return worksheet(&d_->worksheets_[index]);
}

// TODO: There should be a better way to do this...
std::size_t workbook::index_from_ws_filename(const std::string &ws_filename)
{
    std::string sheet_index_string(ws_filename);
    sheet_index_string = sheet_index_string.substr(0, sheet_index_string.find('.'));
    sheet_index_string = sheet_index_string.substr(sheet_index_string.find_last_of('/'));
    auto iter = sheet_index_string.end();
    iter--;
    while (isdigit(*iter))
        iter--;
    auto first_digit = static_cast<std::size_t>(iter - sheet_index_string.begin());
    sheet_index_string = sheet_index_string.substr(first_digit + 1);
    auto sheet_index = static_cast<std::size_t>(std::stoll(sheet_index_string) - 1);
    return sheet_index;
}

worksheet workbook::create_sheet(const std::string &title, const relationship &rel)
{
    d_->worksheets_.push_back(detail::worksheet_impl(this, title));

    auto index = index_from_ws_filename(rel.get_target_uri());
    if (index != d_->worksheets_.size() - 1)
    {
        std::swap(d_->worksheets_.back(), d_->worksheets_[index]);
        d_->worksheets_.pop_back();
    }

    return worksheet(&d_->worksheets_[index]);
}

worksheet workbook::create_sheet(std::size_t index, const std::string &title)
{
    auto ws = create_sheet(index);
    ws.set_title(title);

    return ws;
}

worksheet workbook::create_sheet(const std::string &title)
{
    if (title.length() > 31)
    {
        throw sheet_title_exception(title);
    }

    if (std::find_if(title.begin(), title.end(), [](char c) {
            return c == '*' || c == ':' || c == '/' || c == '\\' || c == '?' || c == '[' || c == ']';
        }) != title.end())
    {
        throw sheet_title_exception(title);
    }

    std::string unique_title = title;

    if (std::find_if(d_->worksheets_.begin(), d_->worksheets_.end(), [&](detail::worksheet_impl &ws) {
            return worksheet(&ws).get_title() == unique_title;
        }) != d_->worksheets_.end())
    {
        std::size_t suffix = 1;

        while (std::find_if(d_->worksheets_.begin(), d_->worksheets_.end(), [&](detail::worksheet_impl &ws) {
                   return worksheet(&ws).get_title() == unique_title;
               }) != d_->worksheets_.end())
        {
            unique_title = title + std::to_string(suffix);
            suffix++;
        }
    }

    auto ws = create_sheet();
    ws.set_title(unique_title);

    return ws;
}

encoding workbook::get_encoding() const
{
    return d_->encoding_;
}

workbook::iterator workbook::begin()
{
    return iterator(*this, 0);
}

workbook::iterator workbook::end()
{
    return iterator(*this, d_->worksheets_.size());
}

workbook::const_iterator workbook::begin() const
{
    return cbegin();
}

workbook::const_iterator workbook::end() const
{
    return cend();
}

workbook::const_iterator workbook::cbegin() const
{
    return const_iterator(*this, 0);
}

workbook::const_iterator workbook::cend() const
{
    return const_iterator(*this, d_->worksheets_.size());
}

std::vector<std::string> workbook::get_sheet_names() const
{
    std::vector<std::string> names;

    for (auto ws : *this)
    {
        names.push_back(ws.get_title());
    }

    return names;
}

worksheet workbook::operator[](const std::string &name)
{
    if(!contains(name)) throw xlnt::key_error();
    return get_sheet_by_name(name);
}

worksheet workbook::operator[](std::size_t index)
{
    return worksheet(&d_->worksheets_[index]);
}

void workbook::clear()
{
    d_->worksheets_.clear();
    d_->relationships_.clear();
    d_->active_sheet_index_ = 0;
    d_->properties_ = document_properties();
}

bool workbook::save(std::vector<unsigned char> &data)
{
    excel_serializer serializer(*this);
    serializer.save_virtual_workbook(data);

    return true;
}

bool workbook::save(const std::string &filename)
{
    excel_serializer serializer(*this);
    serializer.save_workbook(filename);

    return true;
}

bool workbook::operator==(std::nullptr_t) const
{
    return d_.get() == nullptr;
}

bool workbook::operator==(const workbook &rhs) const
{
    return d_.get() == rhs.d_.get();
}

const std::vector<relationship> &xlnt::workbook::get_relationships() const
{
    return d_->relationships_;
}

document_properties &workbook::get_properties()
{
    return d_->properties_;
}

const document_properties &workbook::get_properties() const
{
    return d_->properties_;
}

app_properties &workbook::get_app_properties()
{
    return d_->app_properties_;
}

const app_properties &workbook::get_app_properties() const
{
    return d_->app_properties_;
}


void swap(workbook &left, workbook &right)
{
    using std::swap;
    swap(left.d_, right.d_);

    for (auto ws : left)
    {
        ws.set_parent(left);
    }

    for (auto ws : right)
    {
        ws.set_parent(right);
    }
}

workbook &workbook::operator=(workbook other)
{
    swap(*this, other);
    return *this;
}

workbook::workbook(workbook &&other) : workbook()
{
    swap(*this, other);
}

workbook::workbook(const workbook &other) : workbook()
{
    *d_.get() = *other.d_.get();

    for (auto ws : *this)
    {
        ws.set_parent(*this);
    }
}

workbook::~workbook()
{
}

const std::vector<style> &workbook::get_styles() const
{
    return d_->styles_;
}

bool workbook::get_data_only() const
{
    return d_->data_only_;
}

void workbook::set_data_only(bool data_only)
{
    d_->data_only_ = data_only;
}

bool workbook::get_read_only() const
{
    return d_->read_only_;
}

void workbook::set_read_only(bool read_only)
{
    d_->read_only_ = read_only;
}

void workbook::set_code_name(const std::string & /*code_name*/)
{
}

bool workbook::has_loaded_theme() const
{
    return false;
}

const theme &workbook::get_loaded_theme() const
{
    return d_->theme_;
}

std::vector<named_range> workbook::get_named_ranges() const
{
    std::vector<named_range> named_ranges;

    for (auto ws : *this)
    {
        for (auto &ws_named_range : ws.d_->named_ranges_)
        {
            named_ranges.push_back(ws_named_range.second);
        }
    }

    return named_ranges;
}

std::size_t workbook::add_format(const format &style)
{
    auto format_copy = style;
    
    //TODO this is ugly
    if (!style.get_number_format().has_id())
    {
        bool match = false;
        
        for (std::size_t i = 0; i < d_->formats_.size(); i++)
        {
            const auto &current_number_format = d_->formats_.at(i).get_number_format();
            
            if (current_number_format.get_format_string() == format_copy.get_number_format().get_format_string())
            {
                format_copy.set_number_format(current_number_format);
                match = true;
                break;
            }
        }
        
        if (!match)
        {
            format_copy.get_number_format().set_id(d_->next_custom_format_id_++);
        }
    }
    
    // TODO hashmap?
    for (std::size_t i = 0; i < d_->formats_.size(); i++)
    {
        if (d_->formats_[i] == format_copy)
        {
            return i;
        }
    }
    
    d_->formats_.push_back(format_copy);
    
    return d_->formats_.size() - 1;
}

void workbook::clear_styles()
{
    d_->styles_.clear();
}

void workbook::clear_formats()
{
    d_->formats_.clear();
}

format &workbook::get_format(std::size_t format_index)
{
    return d_->formats_.at(format_index);
}

const format &workbook::get_format(std::size_t format_index) const
{
    return d_->formats_.at(format_index);
}

manifest &workbook::get_manifest()
{
    return d_->manifest_;
}

const manifest &workbook::get_manifest() const
{
    return d_->manifest_;
}

const std::vector<relationship> &workbook::get_root_relationships() const
{
    if (d_->root_relationships_.empty())
    {
        d_->root_relationships_.push_back(
            relationship(relationship::type::core_properties, "rId1", "docProps/core.xml"));
        d_->root_relationships_.push_back(
            relationship(relationship::type::extended_properties, "rId2", "docProps/app.xml"));
        d_->root_relationships_.push_back(relationship(relationship::type::office_document, "rId3", "xl/workbook.xml"));
    }

    return d_->root_relationships_;
}

std::vector<text> &workbook::get_shared_strings()
{
    return d_->shared_strings_;
}

const std::vector<text> &workbook::get_shared_strings() const
{
    return d_->shared_strings_;
}

void workbook::add_shared_string(const text &shared, bool allow_duplicates)
{
    if (d_->shared_strings_.empty())
    {
        create_relationship(next_relationship_id(), "sharedStrings.xml", relationship::type::shared_strings);
        d_->manifest_.add_override_type("/xl/sharedStrings.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml");
    }
    
	if (!allow_duplicates)
	{
		//TODO: inefficient, use a set or something?
		for (auto &s : d_->shared_strings_)
		{
			if (s == shared) return;
		}
	}
    
    d_->shared_strings_.push_back(shared);
}

bool workbook::contains(const std::string &sheet_title) const
{
    for(auto ws : *this)
    {
        if(ws.get_title() == sheet_title) return true;
    }
    
    return false;
}

void workbook::set_thumbnail(const std::vector<std::uint8_t> &thumbnail)
{
    d_->thumbnail_.assign(thumbnail.begin(), thumbnail.end());
}

const std::vector<std::uint8_t> &workbook::get_thumbnail() const
{
    return d_->thumbnail_;
}

style &workbook::create_style(const std::string &name)
{
    style style;
    style.set_name(name);
    
    d_->styles_.push_back(style);
    
    return d_->styles_.back();
}

std::vector<format> &workbook::get_formats()
{
    return d_->formats_;
}

const std::vector<format> &workbook::get_formats() const
{
    return d_->formats_;
}

style &workbook::get_style(const std::string &name)
{
    return *std::find_if(d_->styles_.begin(), d_->styles_.end(),
        [&name](const style &s) { return s.get_name() == name; });
}

const style &workbook::get_style(const std::string &name) const
{
    return *std::find_if(d_->styles_.begin(), d_->styles_.end(),
        [&name](const style &s) { return s.get_name() == name; });
}

std::string workbook::next_relationship_id() const
{
    std::size_t i = 1;
    
    while (std::find_if(d_->relationships_.begin(), d_->relationships_.end(),
        [i](const relationship &r) { return r.get_id() == "rId" + std::to_string(i); }) != d_->relationships_.end())
    {
        i++;
    }
    
    return "rId" + std::to_string(i);
}

} // namespace xlnt

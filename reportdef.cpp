#include "reportdef.h"

static QJsonObject sourceToJson(const ReportSource& s){
    QJsonObject o;
    QString t = "table";
    if (s.type == ReportSourceType::Query) t = "query";
    else if (s.type == ReportSourceType::Sql) t = "sql";
    o["type"] = t;
    o["value"] = s.nameOrSql;
    return o;
}
static ReportSource sourceFromJson(const QJsonObject& o){
    ReportSource s;
    const QString t = o.value("type").toString("table").toLower();
    if (t=="table") s.type = ReportSourceType::Table;
    else if (t=="query") s.type = ReportSourceType::Query;
    else s.type = ReportSourceType::Sql;
    s.nameOrSql = o.value("value").toString();
    return s;
}

QJsonObject ReportDef::toJson() const {
    QJsonObject o;
    o["name"] = name;
    o["mainSource"] = sourceToJson(mainSource);

    QJsonArray extras;
    for (const auto& es : extraSources) extras.push_back(sourceToJson(es));
    o["extraSources"] = extras;

    QJsonArray joinsA;
    for (const auto& j : joins) {
        QJsonObject jo;
        jo["leftSource"]  = j.leftSource;
        jo["rightSource"] = j.rightSource;
        jo["leftField"]   = j.leftField;
        jo["rightField"]  = j.rightField;
        jo["leftJoin"]    = j.leftJoin;
        jo["prefixRight"] = j.prefixRight;
        joinsA.push_back(jo);
    }
    o["joins"] = joinsA;

    QJsonArray fieldsA;
    for (const auto& f : fields) {
        QJsonObject fo;
        fo["field"] = f.field;
        fo["alias"] = f.alias;
        fo["width"] = f.width;
        fo["align"] = int(f.align);
        fieldsA.push_back(fo);
    }
    o["fields"] = fieldsA;

    QJsonArray groupsA;
    for (const auto& g : groups) {
        QJsonObject go;
        go["field"] = g.field;
        go["order"] = (g.order==Qt::AscendingOrder ? "asc" : "desc");
        go["pageBreakAfter"] = g.pageBreakAfter;
        groupsA.push_back(go);
    }
    o["groups"] = groupsA;

    QJsonArray sortsA;
    for (const auto& s : sorts) {
        QJsonObject so;
        so["field"] = s.field;
        so["order"] = (s.order==Qt::AscendingOrder ? "asc" : "desc");
        sortsA.push_back(so);
    }
    o["sorts"] = sortsA;

    QJsonArray aggsA;
    for (const auto& a : aggregates) {
        QJsonObject ao;
        ao["field"] = a.field;
        ao["fn"]    = aggToString(a.fn);
        ao["scope"] = a.scope;
        aggsA.push_back(ao);
    }
    o["aggregates"] = aggsA;

    QJsonArray filtsA;
    for (const auto& f : filters) {
        QJsonObject fo;
        fo["expr"] = f.expr;
        filtsA.push_back(fo);
    }
    o["filters"] = filtsA;

    QJsonObject lo;
    lo["template"] = layout.templateName;
    lo["paper"]    = layout.paper;
    lo["marginL"]  = layout.marginL;
    lo["marginT"]  = layout.marginT;
    lo["marginR"]  = layout.marginR;
    lo["marginB"]  = layout.marginB;
    lo["rowHeight"]= layout.rowHeight;
    lo["zebra"]    = layout.zebra;
    lo["showHeader"]= layout.showHeader;
    lo["showFooter"]= layout.showFooter;
    o["layout"] = lo;

    return o;
}

ReportDef ReportDef::fromJson(const QJsonObject& o) {
    ReportDef d;
    d.name = o.value("name").toString();

    d.mainSource = sourceFromJson(o.value("mainSource").toObject());

    for (auto es : o.value("extraSources").toArray())
        d.extraSources.push_back(sourceFromJson(es.toObject()));

    for (auto jv : o.value("joins").toArray()) {
        const auto jo = jv.toObject();
        JoinDef j;
        j.leftSource  = jo.value("leftSource").toString("main");
        j.rightSource = jo.value("rightSource").toString();
        j.leftField   = jo.value("leftField").toString();
        j.rightField  = jo.value("rightField").toString();
        j.leftJoin    = jo.value("leftJoin").toBool(true);
        j.prefixRight = jo.value("prefixRight").toString("r_");
        d.joins.push_back(j);
    }

    for (auto fv : o.value("fields").toArray()) {
        const auto fo = fv.toObject();
        ReportField f;
        f.field = fo.value("field").toString();
        f.alias = fo.value("alias").toString();
        f.width = fo.value("width").toInt(120);
        f.align = Qt::Alignment(fo.value("align").toInt(int(Qt::AlignLeft)));
        d.fields.push_back(f);
    }

    for (auto gv : o.value("groups").toArray()) {
        const auto go = gv.toObject();
        GroupDef g;
        g.field = go.value("field").toString();
        g.order = (go.value("order").toString("asc")=="asc")? Qt::AscendingOrder : Qt::DescendingOrder;
        g.pageBreakAfter = go.value("pageBreakAfter").toBool(false);
        d.groups.push_back(g);
    }

    for (auto sv : o.value("sorts").toArray()) {
        const auto so = sv.toObject();
        SortDef s;
        s.field = so.value("field").toString();
        s.order = (so.value("order").toString("asc")=="asc")? Qt::AscendingOrder : Qt::DescendingOrder;
        d.sorts.push_back(s);
    }

    for (auto av : o.value("aggregates").toArray()) {
        const auto ao = av.toObject();
        AggDef a;
        a.field = ao.value("field").toString();
        a.fn    = aggFromString(ao.value("fn").toString("none"));
        a.scope = ao.value("scope").toString("report");
        d.aggregates.push_back(a);
    }

    for (auto fv : o.value("filters").toArray()) {
        const auto fo = fv.toObject();
        FilterDef f;
        f.expr = fo.value("expr").toString();
        d.filters.push_back(f);
    }

    const auto lo = o.value("layout").toObject();
    d.layout.templateName = lo.value("template").toString("tabular");
    d.layout.paper        = lo.value("paper").toString("Letter");
    d.layout.marginL      = lo.value("marginL").toInt(12);
    d.layout.marginT      = lo.value("marginT").toInt(12);
    d.layout.marginR      = lo.value("marginR").toInt(12);
    d.layout.marginB      = lo.value("marginB").toInt(12);
    d.layout.rowHeight    = lo.value("rowHeight").toInt(22);
    d.layout.zebra        = lo.value("zebra").toBool(true);
    d.layout.showHeader   = lo.value("showHeader").toBool(true);
    d.layout.showFooter   = lo.value("showFooter").toBool(true);

    return d;
}
